/* SPDX-License-Identifier: MPL-2.0 */

#include "imu.h"

#include <Fusion.h>
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "bus.h"
#if defined(CONFIG_OSKEY_DISPLAY) && defined(CONFIG_OSKEY_RUST)
#include "entropy/entropy.h"
#endif

LOG_MODULE_REGISTER(app_imu);

#define IMU_NODE DT_ALIAS(imu0)

#define IMU_SAMPLE_FREQUENCY_HZ DT_PROP(IMU_NODE, accel_odr)
#define IMU_SAMPLE_PERIOD_US    DIV_ROUND_CLOSEST(USEC_PER_SEC, IMU_SAMPLE_FREQUENCY_HZ)
#define IMU_COMMAND_QUEUE_DEPTH 4
#define IMU_MICRO_G_PER_G       1000000.0f
#define IMU_10UDEG_PER_DEGREE   100000.0f

BUILD_ASSERT(DT_PROP(IMU_NODE, accel_odr) == DT_PROP(IMU_NODE, gyro_odr),
	     "accelerometer and gyroscope ODR must match for fusion");

static const struct device *const imu_dev = DEVICE_DT_GET(IMU_NODE);
static enum app_imu_state imu_state = APP_IMU_DISABLED;

enum imu_event {
	IMU_EVENT_NONE,
	IMU_EVENT_START,
	IMU_EVENT_STOP,
	IMU_EVENT_SAMPLE,
};

enum imu_smf_state {
	IMU_SMF_IDLE,
	IMU_SMF_STREAMING,
	IMU_SMF_INITIALIZING,
	IMU_SMF_READY,
	IMU_SMF_ERROR,
};

struct imu_runtime {
	/* The SMF context must be the first member. */
	struct smf_ctx ctx;
	FusionAhrs ahrs;
	FusionBias bias;
	int64_t last_sample_ticks;
	enum imu_event event;
	enum app_imu_state public_state;
	int last_error;
	int errors;
};

static const struct smf_state imu_states[];

K_MSGQ_DEFINE(imu_command_queue, sizeof(struct app_imu_command), IMU_COMMAND_QUEUE_DEPTH,
	      __alignof__(struct app_imu_command));

static void imu_publish_state(enum app_imu_state state)
{
	if (state == imu_state) {
		return;
	}
	int ret = zbus_chan_pub(&app_imu_state_chan, &state, K_MSEC(100));

	if (ret < 0) {
		LOG_ERR("IMU state publish failed: %d", ret);
		return;
	}
	imu_state = state;
}

static void imu_fusion_initialize(struct imu_runtime *runtime)
{
	FusionAhrsInitialise(&runtime->ahrs);
	FusionAhrsSettings ahrs_settings = fusionAhrsDefaultSettings;

	ahrs_settings.sampleRate = IMU_SAMPLE_FREQUENCY_HZ;
	ahrs_settings.gyroscopeRange = DT_PROP(IMU_NODE, gyro_fs);
	ahrs_settings.accelerationRejection = 10.0f;
	ahrs_settings.magneticRejection = 0.0f;
	ahrs_settings.rejectionTimeout = 5.0f;
	FusionAhrsSetSettings(&runtime->ahrs, &ahrs_settings);

	FusionBiasInitialise(&runtime->bias);
	FusionBiasSettings bias_settings = fusionBiasDefaultSettings;

	bias_settings.sampleRate = IMU_SAMPLE_FREQUENCY_HZ;
	FusionBiasSetSettings(&runtime->bias, &bias_settings);
}

static void imu_fusion_reset_sample_timing(struct imu_runtime *runtime)
{
	runtime->last_sample_ticks = 0;
	FusionAhrsSetSamplePeriod(&runtime->ahrs, 1.0f / (float)IMU_SAMPLE_FREQUENCY_HZ);
}

static void imu_set_public_state(struct imu_runtime *runtime, enum app_imu_state state)
{
	runtime->public_state = state;
	imu_publish_state(state);
}

static FusionVector imu_accel_to_fusion(const struct sensor_value sensor[3])
{
	return (FusionVector){.axis = {
				      .x = (float)sensor_ms2_to_ug(&sensor[1]) / IMU_MICRO_G_PER_G,
				      .y = -(float)sensor_ms2_to_ug(&sensor[0]) / IMU_MICRO_G_PER_G,
				      .z = (float)sensor_ms2_to_ug(&sensor[2]) / IMU_MICRO_G_PER_G,
			      }};
}

static FusionVector imu_gyro_to_fusion(const struct sensor_value sensor[3])
{
	return (FusionVector){
		.axis = {
			.x = (float)sensor_rad_to_10udegrees(&sensor[1]) / IMU_10UDEG_PER_DEGREE,
			.y = -(float)sensor_rad_to_10udegrees(&sensor[0]) / IMU_10UDEG_PER_DEGREE,
			.z = (float)sensor_rad_to_10udegrees(&sensor[2]) / IMU_10UDEG_PER_DEGREE,
		}};
}

static void imu_quaternion_to_render_matrix(FusionQuaternion quaternion, float rotation[9])
{
	quaternion.element.y = -quaternion.element.y;
	quaternion.element.z = -quaternion.element.z;
	const FusionMatrix matrix = FusionQuaternionToMatrix(quaternion);

	memcpy(rotation, matrix.array, sizeof(matrix.array));
}

static int imu_process_sample(struct imu_runtime *runtime)
{
	struct sensor_value accel_sensor[3];
	struct sensor_value gyro_sensor[3];
	int ret = sensor_sample_fetch(imu_dev);

	if (ret < 0) {
		LOG_ERR("IMU sample fetch failed: %d", ret);
		imu_fusion_reset_sample_timing(runtime);
		return ret;
	}

	if (sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel_sensor) < 0 ||
	    sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro_sensor) < 0) {
		LOG_ERR("IMU channel read failed");
		imu_fusion_reset_sample_timing(runtime);
		return -EIO;
	}

#if defined(CONFIG_OSKEY_DISPLAY) && defined(CONFIG_OSKEY_RUST)
	struct app_entropy_snapshot entropy_snapshot;
	if (app_entropy_snapshot_get(&entropy_snapshot) == 0 &&
	    entropy_snapshot.state == APP_ENTROPY_CAPTURING &&
	    entropy_snapshot.current == APP_ENTROPY_SOURCE_IMU) {
		uint8_t raw[6 * sizeof(uint32_t)];

		for (size_t axis = 0; axis < 3; axis++) {
			sys_put_le32((uint32_t)sensor_ms2_to_ug(&accel_sensor[axis]),
				     &raw[axis * sizeof(uint32_t)]);
			sys_put_le32((uint32_t)sensor_rad_to_10udegrees(&gyro_sensor[axis]),
				     &raw[(axis + 3U) * sizeof(uint32_t)]);
		}
		(void)app_entropy_feed(entropy_snapshot.session, APP_ENTROPY_SOURCE_IMU, raw,
				       sizeof(raw), 1U);
	}
#endif
	const int64_t sample_ticks = k_uptime_ticks();

	const FusionVector accel = imu_accel_to_fusion(accel_sensor);
	FusionVector gyro = imu_gyro_to_fusion(gyro_sensor);

	gyro = FusionBiasUpdate(&runtime->bias, gyro);
	if (runtime->last_sample_ticks != 0) {
		const int64_t elapsed_ticks = sample_ticks - runtime->last_sample_ticks;
		const float sample_period =
			(float)k_ticks_to_us_floor64(elapsed_ticks) / USEC_PER_SEC;

		FusionAhrsSetSamplePeriod(&runtime->ahrs, sample_period);
	}
	runtime->last_sample_ticks = sample_ticks;
	FusionAhrsUpdateNoMagnetometer(&runtime->ahrs, gyro, accel);

	struct app_imu_sample sample = {
		.valid = !FusionAhrsGetFlags(&runtime->ahrs).startup,
	};

	imu_quaternion_to_render_matrix(FusionAhrsGetQuaternion(&runtime->ahrs), sample.rotation);

	ret = zbus_chan_pub(&app_imu_sample_chan, &sample, K_MSEC(100));
	if (ret < 0) {
		LOG_ERR("IMU sample publish failed: %d", ret);
	}
	return ret;
}

static bool imu_runtime_streaming(const struct imu_runtime *runtime)
{
	return smf_get_current_leaf_state(SMF_CTX(runtime)) != &imu_states[IMU_SMF_IDLE];
}

static void imu_idle_entry(void *object)
{
	struct imu_runtime *runtime = object;

	imu_set_public_state(runtime, APP_IMU_IDLE);
}

static enum smf_state_result imu_idle_run(void *object)
{
	struct imu_runtime *runtime = object;

	if (runtime->event == IMU_EVENT_START) {
		smf_set_state(SMF_CTX(runtime), &imu_states[IMU_SMF_INITIALIZING]);
	}
	return SMF_EVENT_HANDLED;
}

static void imu_streaming_entry(void *object)
{
	struct imu_runtime *runtime = object;

	FusionAhrsRestart(&runtime->ahrs);
	imu_fusion_reset_sample_timing(runtime);
	runtime->errors = 0;
}

static enum smf_state_result imu_streaming_run(void *object)
{
	struct imu_runtime *runtime = object;

	if (runtime->event == IMU_EVENT_STOP) {
		smf_set_state(SMF_CTX(runtime), &imu_states[IMU_SMF_IDLE]);
		return SMF_EVENT_HANDLED;
	}
	/* START is idempotent while the sensor is already streaming. */
	return runtime->event == IMU_EVENT_START ? SMF_EVENT_HANDLED : SMF_EVENT_PROPAGATE;
}

static void imu_initializing_entry(void *object)
{
	imu_set_public_state(object, APP_IMU_INITIALIZING);
}

static void imu_ready_entry(void *object)
{
	imu_set_public_state(object, APP_IMU_READY);
}

static void imu_error_entry(void *object)
{
	struct imu_runtime *runtime = object;

	imu_set_public_state(runtime, APP_IMU_ERROR);
#if defined(CONFIG_OSKEY_DISPLAY) && defined(CONFIG_OSKEY_RUST)
	struct app_entropy_snapshot entropy_snapshot;

	if (app_entropy_snapshot_get(&entropy_snapshot) == 0 &&
	    entropy_snapshot.state == APP_ENTROPY_CAPTURING &&
	    entropy_snapshot.current == APP_ENTROPY_SOURCE_IMU) {
		(void)app_entropy_fail(entropy_snapshot.session, APP_ENTROPY_SOURCE_IMU,
				       runtime->last_error);
	}
#endif
}

static enum smf_state_result imu_sampling_run(void *object)
{
	struct imu_runtime *runtime = object;

	if (runtime->event != IMU_EVENT_SAMPLE) {
		return SMF_EVENT_PROPAGATE;
	}

	int ret = imu_process_sample(runtime);

	if (ret == 0) {
		runtime->errors = 0;
		const struct smf_state *next = FusionAhrsGetFlags(&runtime->ahrs).startup
						       ? &imu_states[IMU_SMF_INITIALIZING]
						       : &imu_states[IMU_SMF_READY];

		if (smf_get_current_leaf_state(SMF_CTX(runtime)) != next) {
			smf_set_state(SMF_CTX(runtime), next);
		}
		return SMF_EVENT_HANDLED;
	}

	LOG_ERR("IMU sample processing failed: %d", ret);
	runtime->last_error = ret;
	runtime->errors++;
	if (runtime->errors >= 5 &&
	    smf_get_current_leaf_state(SMF_CTX(runtime)) != &imu_states[IMU_SMF_ERROR]) {
		smf_set_state(SMF_CTX(runtime), &imu_states[IMU_SMF_ERROR]);
	}
	return SMF_EVENT_HANDLED;
}

static const struct smf_state imu_states[] = {
	[IMU_SMF_IDLE] = SMF_CREATE_STATE(imu_idle_entry, imu_idle_run, NULL, NULL, NULL),
	[IMU_SMF_STREAMING] =
		SMF_CREATE_STATE(imu_streaming_entry, imu_streaming_run, NULL, NULL, NULL),
	[IMU_SMF_INITIALIZING] = SMF_CREATE_STATE(imu_initializing_entry, imu_sampling_run, NULL,
						  &imu_states[IMU_SMF_STREAMING], NULL),
	[IMU_SMF_READY] = SMF_CREATE_STATE(imu_ready_entry, imu_sampling_run, NULL,
					   &imu_states[IMU_SMF_STREAMING], NULL),
	[IMU_SMF_ERROR] = SMF_CREATE_STATE(imu_error_entry, imu_sampling_run, NULL,
					   &imu_states[IMU_SMF_STREAMING], NULL),
};

static void imu_command_listener(const struct zbus_channel *chan)
{
	const struct app_imu_command *command = zbus_chan_const_msg(chan);

	if (k_msgq_put(&imu_command_queue, command, K_NO_WAIT) < 0) {
		LOG_ERR("IMU command queue full");
	}
}

ZBUS_LISTENER_DEFINE(imu_command_listener_ob, imu_command_listener);
ZBUS_CHAN_ADD_OBS(app_imu_command_chan, imu_command_listener_ob, 0);

static void imu_thread(void *first, void *second, void *third)
{
	ARG_UNUSED(first);
	ARG_UNUSED(second);
	ARG_UNUSED(third);

	struct imu_runtime runtime = {0};

	imu_fusion_initialize(&runtime);
	smf_set_initial(SMF_CTX(&runtime), &imu_states[IMU_SMF_IDLE]);

	while (true) {
		struct app_imu_command command;
		const k_timeout_t timeout =
			imu_runtime_streaming(&runtime) ? K_USEC(IMU_SAMPLE_PERIOD_US) : K_FOREVER;

		if (k_msgq_get(&imu_command_queue, &command, timeout) == 0) {
			switch (command.kind) {
			case APP_IMU_COMMAND_START:
				runtime.event = IMU_EVENT_START;
				break;
			case APP_IMU_COMMAND_STOP:
				runtime.event = IMU_EVENT_STOP;
				break;
			default:
				runtime.event = IMU_EVENT_NONE;
				break;
			}
		} else {
			runtime.event = IMU_EVENT_SAMPLE;
		}

		(void)smf_run_state(SMF_CTX(&runtime));
		runtime.event = IMU_EVENT_NONE;
		/* Retry a failed zbus publication on the next state-machine iteration. */
		imu_publish_state(runtime.public_state);
	}
}

K_THREAD_DEFINE(imu_thread_id, CONFIG_OSKEY_IMU_THREAD_STACK_SIZE, imu_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(K_LOWEST_APPLICATION_THREAD_PRIO), 0, SYS_FOREVER_MS);

int app_imu_init(void)
{
	if (!device_is_ready(imu_dev)) {
		LOG_ERR("IMU device is not ready");
		imu_publish_state(APP_IMU_ERROR);
		return -ENODEV;
	}

	imu_publish_state(APP_IMU_INITIALIZING);
	k_thread_start(imu_thread_id);
	return 0;
}

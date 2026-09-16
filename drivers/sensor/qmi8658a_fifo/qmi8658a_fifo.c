/* SPDX-License-Identifier: MPL-2.0 */

#define DT_DRV_COMPAT oskey_qmi8658a_fifo

#include <errno.h>
#include <oskey/imu_source.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(qmi8658a_fifo);

#define QMI8658A_REG_WHO_AM_I     0x00
#define QMI8658A_REG_CTRL1        0x02
#define QMI8658A_REG_CTRL2        0x03
#define QMI8658A_REG_CTRL3        0x04
#define QMI8658A_REG_CTRL7        0x08
#define QMI8658A_REG_CTRL8        0x09
#define QMI8658A_REG_CTRL9        0x0a
#define QMI8658A_REG_FIFO_CTRL    0x14
#define QMI8658A_REG_FIFO_COUNT   0x15
#define QMI8658A_REG_FIFO_DATA    0x17
#define QMI8658A_REG_STATUSINT    0x2d
#define QMI8658A_REG_RESET_RESULT 0x4d
#define QMI8658A_REG_RESET        0x60

#define QMI8658A_CHIP_ID          0x05
#define QMI8658A_RESET_CMD        0xb0
#define QMI8658A_RESET_OK         0x80
#define QMI8658A_CTRL9_ACK        0x00
#define QMI8658A_CTRL9_RESET_FIFO 0x04
#define QMI8658A_CTRL9_REQ_FIFO   0x05

#define QMI8658A_CTRL1_ADDR_AI         BIT(6)
#define QMI8658A_CTRL7_GYRO_ENABLE     BIT(1)
#define QMI8658A_CTRL7_ACCEL_ENABLE    BIT(0)
#define QMI8658A_CTRL8_CTRL9_STATUSINT BIT(7)
#define QMI8658A_FIFO_CTRL_SIZE_16     (0U << 2)
#define QMI8658A_FIFO_CTRL_STREAM      2U
#define QMI8658A_FIFO_STATUS_OVFLOW    BIT(5)
#define QMI8658A_STATUSINT_CMD_DONE    BIT(7)

#define QMI8658A_FIFO_FRAME_BYTES 12U
#define QMI8658A_CTRL9_POLL_US    1000U
#define QMI8658A_CTRL9_TIMEOUT_US 100000U
#define QMI8658A_RESET_DELAY_MS   15U
#define QMI8658A_GYRO_WAKEUP_US   150000U
#define QMI8658A_COUNTS_PER_AXIS  32768LL
#define QMI8658A_MICRO_G_PER_G    1000000LL
#define QMI8658A_10UDEG_PER_DEG   100000LL

struct qmi8658a_odr {
	uint16_t requested_hz;
	uint32_t millihz;
	uint8_t reg;
};

static const struct qmi8658a_odr qmi8658a_odrs[] = {
	{7174, 7174400, 0x0}, {3587, 3587200, 0x1}, {1793, 1793600, 0x2},
	{896, 896800, 0x3},   {448, 448400, 0x4},   {224, 224200, 0x5},
	{112, 112100, 0x6},   {56, 56050, 0x7},     {28, 28025, 0x8},
};

struct qmi8658a_fifo_frame {
	uint16_t accel[3];
	uint16_t gyro[3];
};

BUILD_ASSERT(sizeof(struct qmi8658a_fifo_frame) == QMI8658A_FIFO_FRAME_BYTES);

struct qmi8658a_fifo_config {
	struct i2c_dt_spec i2c;
	uint16_t accel_fs;
	uint16_t gyro_fs;
	uint16_t accel_odr;
	uint16_t gyro_odr;
	uint32_t poll_interval_us;
};

struct qmi8658a_fifo_data {
	uint64_t last_timestamp_ns;
	uint32_t odr_millihz;
	uint32_t period_ns;
	uint32_t startup_delay_us;
	uint8_t accel_ctrl2;
	uint8_t gyro_ctrl3;
	bool running;
};

static int qmi8658a_reg_read(const struct qmi8658a_fifo_config *cfg, uint8_t reg, uint8_t *data,
			     size_t length)
{
	return i2c_burst_read_dt(&cfg->i2c, reg, data, length);
}

static int qmi8658a_reg_write(const struct qmi8658a_fifo_config *cfg, uint8_t reg, uint8_t value)
{
	return i2c_reg_write_byte_dt(&cfg->i2c, reg, value);
}

static int qmi8658a_ctrl9_wait(const struct qmi8658a_fifo_config *cfg, bool done)
{
	uint8_t status;
	int ret;

	for (uint32_t waited = 0U; waited < QMI8658A_CTRL9_TIMEOUT_US;
	     waited += QMI8658A_CTRL9_POLL_US) {
		ret = qmi8658a_reg_read(cfg, QMI8658A_REG_STATUSINT, &status, 1U);
		if (ret < 0) {
			return ret;
		}
		if (((status & QMI8658A_STATUSINT_CMD_DONE) != 0U) == done) {
			return 0;
		}
		k_usleep(QMI8658A_CTRL9_POLL_US);
	}

	return -ETIMEDOUT;
}

static int qmi8658a_ctrl9(const struct device *dev, uint8_t command)
{
	const struct qmi8658a_fifo_config *cfg = dev->config;
	int ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL9, command);

	if (ret == 0) {
		ret = qmi8658a_ctrl9_wait(cfg, true);
		if (ret < 0) {
			LOG_WRN("CTRL9 command 0x%02x did not complete: %d", command, ret);
		}
	}
	if (ret == 0) {
		ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL9, QMI8658A_CTRL9_ACK);
	}
	if (ret == 0) {
		ret = qmi8658a_ctrl9_wait(cfg, false);
		if (ret < 0) {
			LOG_WRN("CTRL9 command 0x%02x ACK did not clear: %d", command, ret);
		}
	}
	return ret;
}

static int qmi8658a_select_odr(uint16_t requested_hz, uint32_t *millihz, uint8_t *reg)
{
	uint32_t best_delta = UINT32_MAX;
	size_t best = 0U;

	if (requested_hz == 0U) {
		return -EINVAL;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(qmi8658a_odrs); i++) {
		const uint32_t candidate = qmi8658a_odrs[i].requested_hz;
		const uint32_t delta = candidate > requested_hz ? candidate - requested_hz
								: requested_hz - candidate;

		if (delta < best_delta) {
			best_delta = delta;
			best = i;
		}
	}

	*millihz = qmi8658a_odrs[best].millihz;
	*reg = qmi8658a_odrs[best].reg;
	return 0;
}

static int qmi8658a_accel_fs_reg(uint16_t fs, uint8_t *reg)
{
	switch (fs) {
	case 2:
		*reg = 0U;
		return 0;
	case 4:
		*reg = 1U;
		return 0;
	case 8:
		*reg = 2U;
		return 0;
	case 16:
		*reg = 3U;
		return 0;
	default:
		return -EINVAL;
	}
}

static int qmi8658a_gyro_fs_reg(uint16_t fs, uint8_t *reg)
{
	switch (fs) {
	case 16:
		*reg = 0U;
		return 0;
	case 32:
		*reg = 1U;
		return 0;
	case 64:
		*reg = 2U;
		return 0;
	case 128:
		*reg = 3U;
		return 0;
	case 256:
		*reg = 4U;
		return 0;
	case 512:
		*reg = 5U;
		return 0;
	case 1024:
		*reg = 6U;
		return 0;
	case 2048:
		*reg = 7U;
		return 0;
	default:
		return -EINVAL;
	}
}

static uint64_t qmi8658a_timestamp_now(void)
{
	return k_ticks_to_ns_floor64(k_uptime_ticks());
}

static int qmi8658a_start(const struct device *dev)
{
	const struct qmi8658a_fifo_config *cfg = dev->config;
	struct qmi8658a_fifo_data *data = dev->data;
	int ret;

	if (data->running) {
		return 0;
	}

	ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL7, 0U);
	if (ret == 0) {
		ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL1, QMI8658A_CTRL1_ADDR_AI);
	}
	if (ret == 0) {
		ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL8, QMI8658A_CTRL8_CTRL9_STATUSINT);
	}
	if (ret == 0) {
		ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL2, data->accel_ctrl2);
	}
	if (ret == 0) {
		ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL3, data->gyro_ctrl3);
	}
	if (ret == 0) {
		ret = qmi8658a_reg_write(cfg, QMI8658A_REG_FIFO_CTRL,
					 QMI8658A_FIFO_CTRL_SIZE_16 | QMI8658A_FIFO_CTRL_STREAM);
	}
	if (ret == 0) {
		ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL7,
					 QMI8658A_CTRL7_GYRO_ENABLE | QMI8658A_CTRL7_ACCEL_ENABLE);
	}
	if (ret == 0) {
		/* Datasheet section 7.3: gyro turn-on is 150 ms + 3/ODR. */
		k_usleep(data->startup_delay_us);
		ret = qmi8658a_ctrl9(dev, QMI8658A_CTRL9_RESET_FIFO);
	}
	if (ret < 0) {
		(void)qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL7, 0U);
		(void)qmi8658a_reg_write(cfg, QMI8658A_REG_FIFO_CTRL, 0U);
		return ret;
	}

	data->last_timestamp_ns = 0U;
	data->running = true;
	return 0;
}

static int qmi8658a_stop(const struct device *dev)
{
	const struct qmi8658a_fifo_config *cfg = dev->config;
	struct qmi8658a_fifo_data *data = dev->data;
	int first_error = 0;
	int ret;

	if (!data->running) {
		return 0;
	}

	ret = qmi8658a_reg_write(cfg, QMI8658A_REG_CTRL7, 0U);
	if (ret < 0) {
		first_error = ret;
	}
	ret = qmi8658a_reg_write(cfg, QMI8658A_REG_FIFO_CTRL, 0U);
	if ((ret < 0) && (first_error == 0)) {
		first_error = ret;
	}

	data->last_timestamp_ns = 0U;
	data->running = false;
	return first_error;
}

static int qmi8658a_fifo_read_raw(const struct device *dev, struct qmi8658a_fifo_frame *frames,
				  size_t capacity, size_t *count, bool *overflow,
				  uint64_t *snapshot_timestamp_ns)
{
	const struct qmi8658a_fifo_config *cfg = dev->config;
	struct qmi8658a_fifo_data *data = dev->data;
	uint8_t count_status[2];
	const uint8_t fifo_ctrl = QMI8658A_FIFO_CTRL_SIZE_16 | QMI8658A_FIFO_CTRL_STREAM;
	uint16_t fifo_words;
	size_t fifo_bytes;
	size_t read_samples;
	int ret;

	*count = 0U;
	*overflow = false;
	if (!data->running) {
		return -EACCES;
	}

	ret = qmi8658a_reg_read(cfg, QMI8658A_REG_FIFO_COUNT, count_status, sizeof(count_status));
	if (ret < 0) {
		return ret;
	}
	*snapshot_timestamp_ns = qmi8658a_timestamp_now();

	fifo_words = (uint16_t)count_status[0] | ((uint16_t)(count_status[1] & 0x03U) << 8);
	fifo_bytes = (size_t)fifo_words * 2U;
	*overflow = (count_status[1] & QMI8658A_FIFO_STATUS_OVFLOW) != 0U;
	if (*overflow) {
		LOG_WRN("FIFO reports dropped data");
	}
	if (fifo_bytes == 0U) {
		return 0;
	}
	if ((fifo_bytes % QMI8658A_FIFO_FRAME_BYTES) != 0U) {
		return 0;
	}

	read_samples = fifo_bytes / QMI8658A_FIFO_FRAME_BYTES;
	if (read_samples > capacity) {
		return -EOVERFLOW;
	}
	ret = qmi8658a_ctrl9(dev, QMI8658A_CTRL9_REQ_FIFO);
	if (ret < 0) {
		return ret;
	}

	ret = qmi8658a_reg_read(cfg, QMI8658A_REG_FIFO_DATA, (uint8_t *)frames,
				read_samples * QMI8658A_FIFO_FRAME_BYTES);
	const int release_ret = qmi8658a_reg_write(cfg, QMI8658A_REG_FIFO_CTRL, fifo_ctrl);

	if (ret < 0) {
		return ret;
	}
	if (release_ret < 0) {
		return release_ret;
	}

	*count = read_samples;
	return 0;
}

static int32_t qmi8658a_accel_to_ug(int16_t raw, uint16_t fs)
{
	return (int32_t)(((int64_t)raw * fs * QMI8658A_MICRO_G_PER_G) / QMI8658A_COUNTS_PER_AXIS);
}

static int32_t qmi8658a_gyro_to_10udps(int16_t raw, uint16_t fs)
{
	return (int32_t)(((int64_t)raw * fs * QMI8658A_10UDEG_PER_DEG) / QMI8658A_COUNTS_PER_AXIS);
}

static int qmi8658a_read_batch(const struct device *dev, struct oskey_imu_source_batch *batch)
{
	const struct qmi8658a_fifo_config *cfg = dev->config;
	struct qmi8658a_fifo_data *data = dev->data;
	struct qmi8658a_fifo_frame frames[OSKEY_IMU_SOURCE_MAX_BATCH];
	uint64_t snapshot_timestamp_ns = 0U;
	size_t count;
	bool overflow;
	int ret;

	batch->count = 0U;
	ret = qmi8658a_fifo_read_raw(dev, frames, ARRAY_SIZE(frames), &count, &overflow,
				     &snapshot_timestamp_ns);
	if ((ret < 0) || (count == 0U)) {
		return ret;
	}

	const uint64_t batch_span_ns = (count - 1U) * (uint64_t)data->period_ns;
	uint64_t base_timestamp_ns =
		snapshot_timestamp_ns > batch_span_ns ? snapshot_timestamp_ns - batch_span_ns : 1U;
	uint64_t timestamp_step_ns = data->period_ns;
	bool discontinuity = overflow;

	if ((data->last_timestamp_ns != 0U) && (base_timestamp_ns <= data->last_timestamp_ns)) {
		const uint64_t available_ns =
			snapshot_timestamp_ns > data->last_timestamp_ns
				? snapshot_timestamp_ns - data->last_timestamp_ns
				: 0U;

		if (available_ns >= count) {
			timestamp_step_ns = available_ns / count;
			base_timestamp_ns = data->last_timestamp_ns + timestamp_step_ns;
		} else {
			discontinuity = true;
		}
	}
	data->last_timestamp_ns = base_timestamp_ns + (count - 1U) * timestamp_step_ns;
	batch->count = count;

	for (size_t i = 0U; i < count; i++) {
		struct oskey_imu_source_sample *sample = &batch->samples[i];

		sample->timestamp_ns = base_timestamp_ns + i * timestamp_step_ns;
		sample->discontinuity = discontinuity && (i == 0U);
		for (size_t axis = 0U; axis < 3U; axis++) {
			const int16_t accel = (int16_t)sys_le16_to_cpu(frames[i].accel[axis]);
			const int16_t gyro = (int16_t)sys_le16_to_cpu(frames[i].gyro[axis]);

			sample->accel_ug[axis] = qmi8658a_accel_to_ug(accel, cfg->accel_fs);
			sample->gyro_10udps[axis] = qmi8658a_gyro_to_10udps(gyro, cfg->gyro_fs);
		}
	}

	return 0;
}

static int qmi8658a_get_info(const struct device *dev, struct oskey_imu_source_info *info)
{
	const struct qmi8658a_fifo_config *cfg = dev->config;
	const struct qmi8658a_fifo_data *data = dev->data;

	*info = (struct oskey_imu_source_info){
		.odr_millihz = data->odr_millihz,
		.poll_interval_us = cfg->poll_interval_us,
		.gyro_fs_dps = cfg->gyro_fs,
	};
	return 0;
}

static const struct oskey_imu_source_driver_api qmi8658a_fifo_api = {
	.get_info = qmi8658a_get_info,
	.start = qmi8658a_start,
	.stop = qmi8658a_stop,
	.read_batch = qmi8658a_read_batch,
};

static int qmi8658a_fifo_init(const struct device *dev)
{
	const struct qmi8658a_fifo_config *cfg = dev->config;
	struct qmi8658a_fifo_data *data = dev->data;
	uint8_t chip_id;
	uint8_t reset_result;
	uint8_t accel_fs_reg;
	uint8_t gyro_fs_reg;
	uint8_t accel_odr_reg;
	uint8_t gyro_odr_reg;
	uint32_t accel_odr_millihz;
	uint32_t gyro_odr_millihz;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		return -ENODEV;
	}
	ret = qmi8658a_accel_fs_reg(cfg->accel_fs, &accel_fs_reg);
	if (ret == 0) {
		ret = qmi8658a_gyro_fs_reg(cfg->gyro_fs, &gyro_fs_reg);
	}
	if (ret == 0) {
		ret = qmi8658a_select_odr(cfg->accel_odr, &accel_odr_millihz, &accel_odr_reg);
	}
	if (ret == 0) {
		ret = qmi8658a_select_odr(cfg->gyro_odr, &gyro_odr_millihz, &gyro_odr_reg);
	}
	if ((ret < 0) || (accel_odr_millihz != gyro_odr_millihz)) {
		return ret < 0 ? ret : -EINVAL;
	}

	ret = qmi8658a_reg_read(cfg, QMI8658A_REG_WHO_AM_I, &chip_id, 1U);
	if (ret < 0) {
		return ret;
	}
	if (chip_id != QMI8658A_CHIP_ID) {
		return -ENODEV;
	}
	ret = qmi8658a_reg_write(cfg, QMI8658A_REG_RESET, QMI8658A_RESET_CMD);
	if (ret < 0) {
		return ret;
	}
	k_msleep(QMI8658A_RESET_DELAY_MS);
	ret = qmi8658a_reg_read(cfg, QMI8658A_REG_RESET_RESULT, &reset_result, 1U);
	if ((ret < 0) || (reset_result != QMI8658A_RESET_OK)) {
		return ret < 0 ? ret : -EIO;
	}
	data->odr_millihz = accel_odr_millihz;
	data->period_ns = (uint32_t)(1000000000000ULL / data->odr_millihz);
	data->startup_delay_us = QMI8658A_GYRO_WAKEUP_US +
				 (uint32_t)DIV_ROUND_UP(3000000000ULL, (uint64_t)data->odr_millihz);
	data->accel_ctrl2 = (accel_fs_reg << 4) | accel_odr_reg;
	data->gyro_ctrl3 = (gyro_fs_reg << 4) | gyro_odr_reg;
	return 0;
}

#define QMI8658A_FIFO_DEFINE(inst)                                                                 \
	static struct qmi8658a_fifo_data qmi8658a_fifo_data_##inst;                                \
	static const struct qmi8658a_fifo_config qmi8658a_fifo_config_##inst = {                   \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.accel_fs = DT_INST_PROP(inst, accel_fs),                                          \
		.gyro_fs = DT_INST_PROP(inst, gyro_fs),                                            \
		.accel_odr = DT_INST_PROP(inst, accel_odr),                                        \
		.gyro_odr = DT_INST_PROP(inst, gyro_odr),                                          \
		.poll_interval_us = DT_INST_PROP(inst, poll_interval_us),                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, qmi8658a_fifo_init, NULL, &qmi8658a_fifo_data_##inst,          \
			      &qmi8658a_fifo_config_##inst, POST_KERNEL,                           \
			      CONFIG_OSKEY_QMI8658A_FIFO_INIT_PRIORITY, &qmi8658a_fifo_api);

DT_INST_FOREACH_STATUS_OKAY(QMI8658A_FIFO_DEFINE)

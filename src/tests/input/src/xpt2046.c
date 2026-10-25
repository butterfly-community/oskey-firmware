/* SPDX-License-Identifier: MPL-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#define TOUCH_NODE DT_NODELABEL(touch)

static const struct device *const touch = DEVICE_DT_GET(TOUCH_NODE);
static const struct gpio_dt_spec irq = GPIO_DT_SPEC_GET(TOUCH_NODE, int_gpios);
static struct k_spinlock position_lock;
static uint16_t raw_x;
static uint16_t raw_y;
static atomic_t fail_next_read;
static atomic_t failed_reads;
static atomic_t conversion_irq;

struct sample {
	int32_t x;
	int32_t y;
	bool pressed;
};

K_MSGQ_DEFINE(samples, sizeof(struct sample), 32, 4);
static struct sample pending;

static void encode(uint8_t *buffer, size_t offset, uint16_t value)
{
	buffer[offset] = value >> 5;
	buffer[offset + 1] = value << 3;
}

static int spi_io(const struct emul *emul, const struct spi_config *config,
		  const struct spi_buf_set *tx, const struct spi_buf_set *rx)
{
	ARG_UNUSED(emul);
	ARG_UNUSED(config);
	ARG_UNUSED(tx);
	if (!rx || rx->count != 1 || rx->buffers[0].len != 9) {
		return -EINVAL;
	}
	if (atomic_cas(&fail_next_read, 1, 0)) {
		atomic_inc(&failed_reads);
		return -EIO;
	}

	uint8_t *buffer = rx->buffers[0].buf;
	k_spinlock_key_t key = k_spin_lock(&position_lock);
	uint16_t x = raw_x;
	uint16_t y = raw_y;

	k_spin_unlock(&position_lock, key);
	memset(buffer, 0, 9);
	encode(buffer, 1, 600);
	encode(buffer, 3, 4095);
	encode(buffer, 5, x);
	encode(buffer, 7, y);

	if (atomic_get(&conversion_irq)) {
		/* PENIRQ changes during conversion while the contact remains held. */
		gpio_emul_input_set_dt(&irq, 1);
		gpio_emul_input_set_dt(&irq, 0);
	}
	return 0;
}

static int emul_init(const struct emul *emul, const struct device *parent)
{
	ARG_UNUSED(emul);
	ARG_UNUSED(parent);
	return 0;
}

static const struct spi_emul_api api = {.io = spi_io};
EMUL_DT_DEFINE(TOUCH_NODE, emul_init, NULL, NULL, &api, NULL);

static void input_event(struct input_event *event, void *user_data)
{
	ARG_UNUSED(user_data);
	if (event->code == INPUT_ABS_X) {
		pending.x = event->value;
	} else if (event->code == INPUT_ABS_Y) {
		pending.y = event->value;
	} else if (event->code == INPUT_BTN_TOUCH && event->sync) {
		pending.pressed = event->value;
		(void)k_msgq_put(&samples, &pending, K_NO_WAIT);
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TOUCH_NODE), input_event, NULL);

static void position(uint16_t x, uint16_t y)
{
	k_spinlock_key_t key = k_spin_lock(&position_lock);

	raw_x = x;
	raw_y = y;
	k_spin_unlock(&position_lock, key);
}

static struct sample next_sample(bool pressed, const struct sample *previous)
{
	int64_t deadline = k_uptime_get() + 300;
	struct sample sample;

	while (true) {
		int64_t remaining = deadline - k_uptime_get();

		if (remaining <= 0 || k_msgq_get(&samples, &sample, K_MSEC(remaining)) != 0) {
			break;
		}
		if (sample.pressed == pressed &&
		    (!previous || sample.x != previous->x || sample.y != previous->y)) {
			return sample;
		}
	}
	zassert_unreachable("Timed out waiting for %s sample", pressed ? "touch" : "release");
	return (struct sample){0};
}

static void release(void)
{
	zassert_ok(gpio_emul_input_set_dt(&irq, 1));
	(void)next_sample(false, NULL);
	struct sample extra;

	zassert_not_equal(k_msgq_get(&samples, &extra, K_MSEC(40)), 0,
			  "Touch events continued after release");
}

static void cleanup(void *fixture)
{
	ARG_UNUSED(fixture);
	gpio_emul_input_set_dt(&irq, 1);
	k_sleep(K_MSEC(150));
	k_msgq_purge(&samples);
	atomic_clear(&fail_next_read);
	atomic_clear(&failed_reads);
	atomic_clear(&conversion_irq);
}

static void prepare(void *fixture)
{
	zassert_true(device_is_ready(touch));
	zassert_true(gpio_is_ready_dt(&irq));
	cleanup(fixture);
	position(800, 900);
}

ZTEST(xpt2046, test_drag_while_irq_stays_active)
{
	zassert_ok(gpio_emul_input_set_dt(&irq, 0));
	struct sample first = next_sample(true, NULL);

	position(1800, 1600);
	struct sample second = next_sample(true, &first);

	position(3000, 2800);
	struct sample third = next_sample(true, &second);

	zassert_not_equal(first.x, third.x);
	zassert_not_equal(first.y, third.y);
	zassert_equal(gpio_pin_get_dt(&irq), 1);
	release();
}

ZTEST(xpt2046, test_spi_recovery_and_conversion_irq)
{
	atomic_set(&conversion_irq, 1);
	zassert_ok(gpio_emul_input_set_dt(&irq, 0));
	struct sample first = next_sample(true, NULL);

	atomic_set(&fail_next_read, 1);
	position(3200, 3000);
	(void)next_sample(true, &first);
	zassert_equal(atomic_get(&failed_reads), 1);
	release();
}

ZTEST(xpt2046, test_repeated_contacts)
{
	for (int contact = 0; contact < 3; contact++) {
		position(1000 + contact * 600, 1100 + contact * 600);
		zassert_ok(gpio_emul_input_set_dt(&irq, 0));
		(void)next_sample(true, NULL);
		release();
	}
}

ZTEST_SUITE(xpt2046, NULL, NULL, prepare, cleanup, NULL);

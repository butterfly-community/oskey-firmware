#ifdef CONFIG_I2S

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/audio/codec.h>
#include <string.h>

#if DT_NODE_EXISTS(DT_NODELABEL(i2s_rxtx))
#define I2S_RX_NODE DT_NODELABEL(i2s_rxtx)
#else
#define I2S_RX_NODE DT_NODELABEL(i2s_rx)
#endif

#define FREQUENCY   44100
#define BIT_WIDTH   16
#define BYTES_PER_SAMPLE   sizeof(int16_t)
#define NUMBER_OF_CHANNELS 1
/* Such block length provides an echo with the delay of 100 ms. */
#define PER_BLOCK  ((FREQUENCY / 10) * NUMBER_OF_CHANNELS)
#define INITIAL_BLOCKS     2
#define TIMEOUT            1000

#define BLOCK_SIZE  (BYTES_PER_SAMPLE * PER_BLOCK)
#define BLOCK_COUNT (INITIAL_BLOCKS + 4)
K_MEM_SLAB_DEFINE_STATIC(mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static bool configure_streams(const struct device *i2s_dev_rx, const struct i2s_config *config)
{
	int ret;

	ret = i2s_configure(i2s_dev_rx, I2S_DIR_RX, config);
	if (ret < 0) {
		printk("Failed to configure RX stream: %d\n", ret);
		return false;
	}

	return true;
}

static bool trigger_command(const struct device *i2s_dev_rx, enum i2s_trigger_cmd cmd)
{
	int ret;
	ret = i2s_trigger(i2s_dev_rx, I2S_DIR_RX, cmd);
	if (ret < 0) {
		printk("Failed to trigger command %d on RX: %d\n", cmd, ret);
		return false;
	}

	return true;
}

int i2s_start(void)
{
	const struct device *const i2s_dev_rx = DEVICE_DT_GET(I2S_RX_NODE);
	struct i2s_config config;

	printk("I2S start\n");

	if (!device_is_ready(i2s_dev_rx)) {
		printk("%s is not ready\n", i2s_dev_rx->name);
		return 0;
	}

	config.word_size = BIT_WIDTH;
	config.channels = NUMBER_OF_CHANNELS;
	config.format = I2S_FMT_DATA_FORMAT_I2S;
	config.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
	config.frame_clk_freq = FREQUENCY;
	config.mem_slab = &mem_slab;
	config.block_size = BLOCK_SIZE;
	config.timeout = TIMEOUT;

	if (!configure_streams(i2s_dev_rx, &config)) {
		return 0;
	}

  printk("Streams started\n");

	for (;;) {
		if (!trigger_command(i2s_dev_rx, I2S_TRIGGER_START)) {
			return 0;
		}

		void *mem_block;
		uint32_t block_size;
		int ret;

		ret = i2s_read(i2s_dev_rx, &mem_block, &block_size);
		if (ret < 0) {
			printk("Failed to read data: %d\n", ret);
			break;
		}

		if (!trigger_command(i2s_dev_rx, I2S_TRIGGER_DROP)) {
			return 0;
		}

    printk("Data read successfully, block size: %u bytes\n", block_size);
	}
}

#endif /* CONFIG_I2S */
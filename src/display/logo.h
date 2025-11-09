#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMAGE_GCC_LOGO
#define LV_ATTRIBUTE_IMAGE_GCC_LOGO
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_GCC_LOGO uint8_t
	logo_map[] = {};
const lv_image_dsc_t logo_image = {
	.header.cf = LV_COLOR_FORMAT_RGB565,
	.header.magic = LV_IMAGE_HEADER_MAGIC,
	.header.w = 800,
	.header.h = 450,
	.data_size = 360000 * 2,
	.data = logo_map,
};

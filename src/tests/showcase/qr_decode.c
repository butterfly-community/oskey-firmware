/* SPDX-License-Identifier: MPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include "quirc.h"

/* Decode a P5 grayscale frame from stdin using the product's QR decoder. */
int main(void)
{
    int width, height, max_value;
    if (scanf("P5 %d %d %d", &width, &height, &max_value) != 3 ||
        max_value != 255 || width < 1 || height < 1 || width > 2048 || height > 2048)
        return 1;
    if (getchar() != '\n') return 1;
    struct quirc *decoder = quirc_new();
    if (decoder == NULL) return 1;
    if (quirc_resize(decoder, width, height) != 0) {
        quirc_destroy(decoder);
        return 1;
    }
    uint8_t *pixels = quirc_begin(decoder, NULL, NULL);
    size_t size = (size_t)width * (size_t)height;
    if (fread(pixels, 1, size, stdin) != size) {
        quirc_destroy(decoder);
        return 1;
    }
    quirc_end(decoder);
    int decoded = 0;
    for (int i = 0; i < quirc_count(decoder); ++i) {
        struct quirc_code code;
        struct quirc_data result;
        quirc_extract(decoder, i, &code);
        if (quirc_decode(&code, &result) == QUIRC_SUCCESS) {
            fwrite(result.payload, 1, result.payload_len, stdout);
            putchar('\n');
            decoded++;
        }
    }
    quirc_destroy(decoder);
    return decoded > 0 ? 0 : 1;
}

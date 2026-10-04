/* SPDX-License-Identifier: MPL-2.0 */
#ifndef OSKEY_SHOWCASE_FIXTURES_H
#define OSKEY_SHOWCASE_FIXTURES_H
#include "ui.h"
#include "entropy/entropy.h"
extern struct AppConfirmation showcase_confirmation;
extern struct app_entropy_snapshot showcase_entropy;
extern int showcase_nxp_state;
extern int showcase_fido_retries;
extern lv_draw_buf_t *showcase_camera_frame;
void showcase_fixture_init(void);
#endif

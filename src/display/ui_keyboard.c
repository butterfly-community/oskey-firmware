/* SPDX-License-Identifier: MPL-2.0 */

#include "ui_keyboard.h"

static void single_input(lv_event_t *event)
{
	lv_obj_t *keyboard = lv_event_get_current_target(event);
	uint32_t button = lv_buttonmatrix_get_selected_button(keyboard);

	/* Layout changes rebuild the controls; restore the policy on the next press. */
	if (button != LV_BUTTONMATRIX_BUTTON_NONE &&
	    !lv_buttonmatrix_has_button_ctrl(keyboard, button, LV_BUTTONMATRIX_CTRL_NO_REPEAT)) {
		lv_buttonmatrix_set_button_ctrl_all(keyboard, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
	}
}

lv_obj_t *ui_keyboard_create(lv_obj_t *parent)
{
	lv_obj_t *keyboard = lv_keyboard_create(parent);

	lv_buttonmatrix_set_button_ctrl_all(keyboard, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
	lv_obj_add_event_cb(keyboard, single_input, LV_EVENT_PRESSED, NULL);
	return keyboard;
}

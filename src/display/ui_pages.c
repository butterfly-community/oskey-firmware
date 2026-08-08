#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "assets/assets.h"

static bool valid_pin(const char *pin)
{
	bool digit = false;
	bool lower = false;
	bool upper = false;
	bool symbol = false;

	if (strlen(pin) < 8) {
		return false;
	}

	for (; *pin != '\0'; ++pin) {
		bool is_digit = *pin >= '0' && *pin <= '9';
		bool is_lower = *pin >= 'a' && *pin <= 'z';
		bool is_upper = *pin >= 'A' && *pin <= 'Z';
		digit |= is_digit;
		lower |= is_lower;
		upper |= is_upper;
		symbol |= *pin >= 32 && *pin <= 126 && !is_digit && !is_lower && !is_upper;
	}
	return digit && lower && upper && symbol;
}

static const struct ui_input_config input_pages[] = {
	[UI_PAGE_LOCKED] =
		{
			.title = "Unlock OSKey",
			.hint = "Enter the PIN for this wallet",
			.placeholder = "Enter PIN",
			.action = "Unlock",
			.action_detail = "Open the hardware wallet",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_PIN_NEW] =
		{
			.title = "Create PIN",
			.hint = "Use upper, lower, number and symbol",
			.placeholder = "Enter PIN",
			.action = "Continue",
			.action_detail = "Confirm this PIN",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_PIN_CONFIRM] =
		{
			.title = "Confirm PIN",
			.hint = "Enter the same PIN again",
			.placeholder = "Enter PIN",
			.action = "Continue",
			.action_detail = "Choose a recovery source",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_FIDO_PIN_RECOVER] =
		{
			.title = "Recover FIDO PIN",
			.hint = "Verify the OSKey wallet PIN",
			.placeholder = "Wallet PIN",
			.action = "Recover",
			.action_detail = "Restore FIDO PIN attempts",
			.max_length = UI_PIN_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_IMPORT] =
		{
			.title = "Import wallet",
			.hint = "Enter the recovery phrase in order",
			.placeholder = "word1 word2 ...",
			.action = "Continue",
			.action_detail = "Configure the mnemonic passphrase",
			.max_length = UI_MNEMONIC_SIZE - 1,
		},
	[UI_PAGE_VERIFY] =
		{
			.title = "Verify phrase",
			.hint = "Enter the recovery phrase again",
			.placeholder = "word1 word2 ...",
			.action = "Continue",
			.action_detail = "Configure the mnemonic passphrase",
			.max_length = UI_MNEMONIC_SIZE - 1,
		},
	[UI_PAGE_PASSPHRASE] =
		{
			.title = "Mnemonic passphrase",
			.hint = "Optional; leave empty to continue without one",
			.placeholder = "Can be empty",
			.action = "Continue",
			.action_detail = "Use this passphrase or leave it empty",
			.max_length = UI_PASSPHRASE_SIZE - 1,
			.password = true,
		},
	[UI_PAGE_PASSPHRASE_CONFIRM] =
		{
			.title = "Confirm passphrase",
			.hint = "Enter the same mnemonic passphrase",
			.placeholder = "Repeat passphrase",
			.action = "Continue",
			.action_detail = "Create the wallet",
			.max_length = UI_PASSPHRASE_SIZE - 1,
			.password = true,
		},
};

static void submit_wallet(void)
{
	char auxiliary[UI_PASSPHRASE_SIZE + UI_PIN_SIZE];
	size_t passphrase_len = strlen(ui.passphrase);
	size_t pin_len = strlen(ui.pin);

	memcpy(auxiliary, ui.passphrase, passphrase_len);
	memcpy(auxiliary + passphrase_len, ui.pin, pin_len);
	ui_submit(LocalRequestKind_InitCustom, (uint32_t)passphrase_len, ui.mnemonic,
		  strlen(ui.mnemonic), auxiliary, passphrase_len + pin_len);
	ui_wipe(auxiliary, sizeof(auxiliary));
}

static void submit_current_input(const char *text)
{
	switch (ui.page) {
	case UI_PAGE_LOCKED:
	case UI_PAGE_FIDO_PIN_RECOVER:
		ui_submit(LocalRequestKind_Unlock, 0, text, strlen(text), NULL, 0);
		break;
	case UI_PAGE_PIN_NEW:
		if (!valid_pin(text)) {
			ui_input_error("Use 8+ characters with upper, lower, number and symbol");
			return;
		}
		snprintf(ui.pin, sizeof(ui.pin), "%s", text);
		ui_push(UI_PAGE_PIN_CONFIRM);
		break;
	case UI_PAGE_PIN_CONFIRM:
		if (strcmp(ui.pin, text) != 0) {
			ui_input_error("PINs do not match");
			return;
		}
		ui_push(UI_PAGE_SOURCE);
		break;
	case UI_PAGE_IMPORT:
		snprintf(ui.mnemonic, sizeof(ui.mnemonic), "%s", text);
		ui_push(UI_PAGE_PASSPHRASE);
		break;
	case UI_PAGE_VERIFY:
		/* Entering "oskey" instead of the phrase is an intentional product option. */
		if (strcmp(text, "oskey") != 0 && strcmp(ui.mnemonic, text) != 0) {
			ui_input_error("Recovery phrase does not match");
			return;
		}
		ui_push(UI_PAGE_PASSPHRASE);
		break;
	case UI_PAGE_PASSPHRASE:
		ui_wipe(ui.passphrase, sizeof(ui.passphrase));
		snprintf(ui.passphrase, sizeof(ui.passphrase), "%s", text);
		if (text[0] == '\0') {
			submit_wallet();
		} else {
			ui_push(UI_PAGE_PASSPHRASE_CONFIRM);
		}
		break;
	case UI_PAGE_PASSPHRASE_CONFIRM:
		if (strcmp(ui.passphrase, text) != 0) {
			ui_input_error("Passphrases do not match");
			return;
		}
		submit_wallet();
		break;
	case UI_PAGE_WIFI_PASSWORD:
		ui_wifi_password_submit(text);
		break;
	default:
		break;
	}
}

static void navigate(lv_event_t *event)
{
	ui_push((enum ui_page)(uintptr_t)lv_event_get_user_data(event));
}

static void open_mnemonic_length(void)
{
	ui.custom_entropy = false;
	ui_push(UI_PAGE_LENGTH);
}

static void generate_mnemonic(lv_event_t *event)
{
	ARG_UNUSED(event);
	if (!ui.features[APP_FEATURE_HARDWARE_RNG]) {
		ui_dialog_show(&oskey_warning, "No hardware RNG",
			       "Test only. Do not use this phrase for real assets.", "Continue",
			       UI_TONE_WARNING, open_mnemonic_length);
		return;
	}

	open_mnemonic_length();
}

static void enable_custom_entropy(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui.custom_entropy = true;
	ui_render();
}

static void select_mnemonic_length(lv_event_t *event)
{
	uint32_t words = (uint32_t)(uintptr_t)lv_event_get_user_data(event);

	ui_submit(LocalRequestKind_GenerateMnemonic, words, NULL, 0, NULL, 0);
}

static void select_entropy_size(lv_event_t *event)
{
	ui.entropy_bits = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
	ui_wipe(ui.entropy, sizeof(ui.entropy));
	ui_push(UI_PAGE_ENTROPY);
}

static void restart_device(void)
{
	ui_submit(LocalRequestKind_Restart, 0, NULL, 0, NULL, 0);
}

static void erase_storage(void)
{
	ui_submit(LocalRequestKind_ResetStorage, 0, NULL, 0, NULL, 0);
}

static void confirm_restart(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_dialog_show(&oskey_refresh, "Restart OSKey?",
		       "The device will disconnect briefly. Stored data will not change.",
		       "Restart", UI_TONE_ACTIVE, restart_device);
}

static void confirm_reset(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_dialog_show(&oskey_trash, "Erase device data?",
		       "Wallet, passkeys and network settings will be permanently removed.",
		       "Erase data", UI_TONE_DANGER, erase_storage);
}

#if defined(CONFIG_OSKEY_AUDIO)
static void audio_beep_clicked(lv_event_t *event)
{
	ARG_UNUSED(event);
	struct app_audio_command command = { .kind = APP_AUDIO_COMMAND_BEEP };

	(void)zbus_chan_pub(&app_audio_command_chan, &command, K_MSEC(100));
}

static void audio_volume_changed(lv_event_t *event)
{
	lv_obj_t *slider = lv_event_get_target(event);
	lv_obj_t *label = lv_event_get_user_data(event);
	struct app_audio_command command = {
		.kind = APP_AUDIO_COMMAND_SET_VOLUME,
		.volume = (uint8_t)lv_slider_get_value(slider),
	};

	(void)zbus_chan_pub(&app_audio_command_chan, &command, K_MSEC(100));
	lv_label_set_text_fmt(label, "Volume %u%%", command.volume);
}
#endif

static void show_audio(void)
{
	lv_obj_t *content = ui_page_begin("Audio", UI_NAVIGATION_BACK);
#if defined(CONFIG_OSKEY_AUDIO)
	ui_section(content, "AUDIO");
	ui_list_row(content, &oskey_audio, "Play beep", "Play a short test sound", NULL,
		    UI_TONE_ACTIVE, audio_beep_clicked, NULL);
	ui_list_row(content, &oskey_audio,
		    ui.status.audio.state == APP_AUDIO_PLAYING ? "Playing" : "Idle",
		    "Audio codec output", NULL,
		    ui.status.audio.state == APP_AUDIO_PLAYING ? UI_TONE_ACTIVE : UI_TONE_SUCCESS,
		    NULL, NULL);

	ui_section(content, "VOLUME");
	lv_obj_t *row = lv_obj_create(content);
	lv_obj_set_width(row, LV_PCT(100));
	lv_obj_set_height(row, LV_SIZE_CONTENT);
	lv_obj_set_style_min_height(row, 44, 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_color(row, lv_color_hex(0x242b33), 0);
	lv_obj_set_style_border_width(row, 1, 0);
	lv_obj_set_style_radius(row, 0, 0);
	lv_obj_set_style_pad_hor(row, 4, 0);
	/* Keep the slider clear of the page scrollbar on the right edge. */
	lv_obj_set_style_pad_right(row, 20, 0);
	lv_obj_set_style_pad_ver(row, 8, 0);
	lv_obj_set_style_pad_column(row, 8, 0);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *volume_label = lv_label_create(row);
	lv_obj_set_style_text_color(volume_label, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(volume_label, UI_FONT_BODY, 0);
	lv_label_set_text_fmt(volume_label, "Volume %u%%", ui.status.audio.volume);

	lv_obj_t *slider = lv_slider_create(row);
	lv_obj_set_flex_grow(slider, 1);
	lv_obj_set_height(slider, LV_DPX(18));
	/* Keep the track well inside so the knob never clips, even when pressed. */
	lv_obj_set_style_pad_hor(slider, LV_DPX(14), LV_PART_MAIN);
	lv_slider_set_range(slider, 0, 100);
	lv_slider_set_value(slider, ui.status.audio.volume, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(slider, lv_color_hex(0x242b33), LV_PART_MAIN);
	lv_obj_set_style_bg_color(slider, lv_color_hex(0x4da3ff), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(slider, lv_color_hex(0xf2f5f7), LV_PART_KNOB);
	lv_obj_add_event_cb(slider, audio_volume_changed, LV_EVENT_VALUE_CHANGED, volume_label);
#endif
}

#if defined(CONFIG_OSKEY_IMU)
static lv_point_precise_t imu_box_proj[8];
static lv_timer_t *imu_box_timer_handle;
static lv_obj_t *imu_box_obj;
static lv_obj_t *imu_pitch_label;
static lv_obj_t *imu_roll_label;
static lv_obj_t *imu_gyro_label;
static lv_obj_t *imu_state_label;
static bool imu_box_paused;
static float imu_box_face_light[6];

static const uint8_t imu_box_edges[12][2] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0},
	{4, 5}, {5, 6}, {6, 7}, {7, 4},
	{0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static const uint8_t imu_box_faces[6][4] = {
	{4, 5, 6, 7}, {0, 1, 2, 3},
	{1, 2, 6, 5}, {0, 3, 7, 4},
	{3, 2, 6, 7}, {0, 1, 5, 4},
};

static const int8_t imu_box_normals[6][3] = {
	{0, 0, 1}, {0, 0, -1},
	{1, 0, 0}, {-1, 0, 0},
	{0, 1, 0}, {0, -1, 0},
};

static void imu_box_project(float pitch, float roll)
{
	/* Board-like cuboid: wider than tall, thin in depth. */
	static const float vertices[8][3] = {
		{-1.8f, -1.0f, -0.4f}, {1.8f, -1.0f, -0.4f}, {1.8f, 1.0f, -0.4f}, {-1.8f, 1.0f, -0.4f},
		{-1.8f, -1.0f, 0.4f},  {1.8f, -1.0f, 0.4f},  {1.8f, 1.0f, 0.4f},  {-1.8f, 1.0f, 0.4f},
	};
	const float pr = pitch * 0.017453292519943295f;
	const float rr = roll * 0.017453292519943295f;
	const float cp = cosf(pr);
	const float sp = sinf(pr);
	const float cr = cosf(rr);
	const float sr = sinf(rr);
	const int32_t scale = 45;

	for (size_t i = 0; i < ARRAY_SIZE(vertices); i++) {
		const float x = vertices[i][0];
		const float y = vertices[i][1];
		const float z = vertices[i][2];
		/* Roll around the Y axis, then pitch around the X axis. */
		const float rx = x * cr + z * sr;
		const float rz = -x * sr + z * cr;
		const float ry = y * cp - rz * sp;

		imu_box_proj[i].x = (int32_t)(rx * scale);
		imu_box_proj[i].y = (int32_t)(ry * scale);
	}

	/* Keep the rotated face normals so the draw pass can shade and cull. */
	for (size_t i = 0; i < ARRAY_SIZE(imu_box_normals); i++) {
		const float nx = imu_box_normals[i][0];
		const float ny = imu_box_normals[i][1];
		const float nz = imu_box_normals[i][2];
		const float rnz = -nx * sr + nz * cr;

		imu_box_face_light[i] = ny * sp + rnz * cp;
	}
}

static lv_color_t imu_box_shade(float light)
{
	/* Brighter faces face the viewer; scale the accent blue accordingly. */
	const float b = 0.30f + 0.70f * light;

	return lv_color_make((uint8_t)(77.0f * b), (uint8_t)(163.0f * b), (uint8_t)(255.0f * b));
}

static void imu_box_draw(lv_event_t *event)
{
	lv_obj_t *obj = lv_event_get_target_obj(event);
	lv_draw_task_t *task = lv_event_get_draw_task(event);
	lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);

	if (imu_box_paused) {
		return;
	}
	if (base == NULL || base->part != LV_PART_MAIN) {
		return;
	}

	lv_layer_t *layer = base->layer;
	lv_area_t coords;
	lv_draw_line_dsc_t line;

	lv_obj_get_coords(obj, &coords);
	int32_t cx = (coords.x1 + coords.x2) / 2;
	int32_t cy = (coords.y1 + coords.y2) / 2;

	lv_draw_line_dsc_init(&line);
	line.color = lv_color_hex(0xbfe0ff);
	line.width = 2;

	lv_draw_triangle_dsc_t tri;

	lv_draw_triangle_dsc_init(&tri);
	tri.opa = LV_OPA_COVER;

	for (size_t i = 0; i < ARRAY_SIZE(imu_box_faces); i++) {
		if (imu_box_face_light[i] <= 0.0f) {
			continue;
		}
		uint8_t v0 = imu_box_faces[i][0];
		uint8_t v1 = imu_box_faces[i][1];
		uint8_t v2 = imu_box_faces[i][2];
		uint8_t v3 = imu_box_faces[i][3];

		tri.color = imu_box_shade(imu_box_face_light[i]);
		tri.p[0] = (lv_point_precise_t){ cx + imu_box_proj[v0].x, cy + imu_box_proj[v0].y };
		tri.p[1] = (lv_point_precise_t){ cx + imu_box_proj[v1].x, cy + imu_box_proj[v1].y };
		tri.p[2] = (lv_point_precise_t){ cx + imu_box_proj[v2].x, cy + imu_box_proj[v2].y };
		lv_draw_triangle(layer, &tri);

		tri.p[0] = (lv_point_precise_t){ cx + imu_box_proj[v0].x, cy + imu_box_proj[v0].y };
		tri.p[1] = (lv_point_precise_t){ cx + imu_box_proj[v2].x, cy + imu_box_proj[v2].y };
		tri.p[2] = (lv_point_precise_t){ cx + imu_box_proj[v3].x, cy + imu_box_proj[v3].y };
		lv_draw_triangle(layer, &tri);
	}

	for (size_t i = 0; i < ARRAY_SIZE(imu_box_edges); i++) {
		uint8_t a = imu_box_edges[i][0];
		uint8_t b = imu_box_edges[i][1];

		line.p1 = (lv_point_precise_t){ cx + imu_box_proj[a].x, cy + imu_box_proj[a].y };
		line.p2 = (lv_point_precise_t){ cx + imu_box_proj[b].x, cy + imu_box_proj[b].y };
		lv_draw_line(layer, &line);
	}
}

static const char *imu_state_text(enum app_imu_state state)
{
	switch (state) {
	case APP_IMU_READY:
		return "Streaming";
	case APP_IMU_IDLE:
		return "Idle";
	case APP_IMU_INITIALIZING:
		return "Initializing";
	case APP_IMU_ERROR:
		return "Error";
	case APP_IMU_DISABLED:
	default:
		return "Disabled";
	}
}

static void imu_box_timer(lv_timer_t *timer)
{
	lv_obj_t *obj = lv_timer_get_user_data(timer);
	struct app_imu_sample sample;
	enum app_imu_state state;

	if (imu_box_paused) {
		return;
	}
	if (zbus_chan_read(&app_imu_state_chan, &state, K_NO_WAIT) == 0) {
		lv_label_set_text(imu_state_label, imu_state_text(state));
	}
	if (zbus_chan_read(&app_imu_sample_chan, &sample, K_NO_WAIT) == 0) {
		char pitch[16];
		char roll[16];
		char gyro[16];
		int pitch_tenths = (int)(sample.pitch * 10.0f);
		int roll_tenths = (int)(sample.roll * 10.0f);
		int pitch_frac = pitch_tenths % 10;
		int roll_frac = roll_tenths % 10;

		imu_box_project(sample.pitch, sample.roll);
		snprintk(pitch, sizeof(pitch), "%+d.%d", pitch_tenths / 10,
			 pitch_frac < 0 ? -pitch_frac : pitch_frac);
		snprintk(roll, sizeof(roll), "%+d.%d", roll_tenths / 10,
			 roll_frac < 0 ? -roll_frac : roll_frac);
		snprintk(gyro, sizeof(gyro), "%+d %+d %+d", (int)sample.gyro_x,
			 (int)sample.gyro_y, (int)sample.gyro_z);
		lv_label_set_text_fmt(imu_pitch_label, "Pitch %s deg", pitch);
		lv_label_set_text_fmt(imu_roll_label, "Roll %s deg", roll);
		lv_label_set_text_fmt(imu_gyro_label, "Gyro %s dps", gyro);
		lv_obj_invalidate(obj);
	}
}

static void imu_box_scroll_begin(lv_event_t *event)
{
	ARG_UNUSED(event);
	imu_box_paused = true;
}

static void imu_box_scroll_end(lv_event_t *event)
{
	ARG_UNUSED(event);
	imu_box_paused = false;
	if (imu_box_timer_handle != NULL) {
		lv_obj_invalidate(imu_box_obj);
	}
}

static void imu_box_delete(lv_event_t *event)
{
	ARG_UNUSED(event);

	struct app_imu_command command = { .kind = APP_IMU_COMMAND_STOP };

	(void)zbus_chan_pub(&app_imu_command_chan, &command, K_MSEC(100));
	if (imu_box_timer_handle != NULL) {
		lv_timer_delete(imu_box_timer_handle);
		imu_box_timer_handle = NULL;
	}
	imu_box_obj = NULL;
}

static void show_imu(void)
{
	lv_obj_t *content = ui_page_begin("Gyro", UI_NAVIGATION_BACK);
	struct app_imu_command command = { .kind = APP_IMU_COMMAND_START };

	(void)zbus_chan_pub(&app_imu_command_chan, &command, K_MSEC(100));
	imu_box_paused = false;
	lv_obj_add_event_cb(content, imu_box_scroll_begin, LV_EVENT_SCROLL_BEGIN, NULL);
	lv_obj_add_event_cb(content, imu_box_scroll_end, LV_EVENT_SCROLL_END, NULL);

	ui_section(content, "IMU");
	imu_state_label = lv_label_create(content);
	lv_obj_set_width(imu_state_label, LV_PCT(100));
	lv_obj_set_style_text_color(imu_state_label, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(imu_state_label, UI_FONT_BODY, 0);
	lv_label_set_text(imu_state_label, imu_state_text(ui.status.imu));

	lv_obj_t *imu_detail = lv_label_create(content);
	lv_obj_set_width(imu_detail, LV_PCT(100));
	lv_obj_set_style_text_color(imu_detail, lv_color_hex(0x929eaa), 0);
	lv_obj_set_style_text_font(imu_detail, UI_FONT_CAPTION, 0);
	lv_label_set_text(imu_detail, "Accelerometer and gyroscope");

	if (imu_box_timer_handle != NULL) {
		lv_timer_delete(imu_box_timer_handle);
		imu_box_timer_handle = NULL;
	}

	imu_box_obj = lv_obj_create(content);
	lv_obj_set_width(imu_box_obj, LV_PCT(100));
	lv_obj_set_height(imu_box_obj, 150);
	lv_obj_set_style_bg_color(imu_box_obj, lv_color_hex(0x14181d), 0);
	lv_obj_set_style_bg_opa(imu_box_obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(imu_box_obj, 8, 0);
	lv_obj_set_style_border_width(imu_box_obj, 1, 0);
	lv_obj_set_style_border_color(imu_box_obj, lv_color_hex(0x242b33), 0);
	lv_obj_set_style_pad_all(imu_box_obj, 0, 0);
	lv_obj_remove_flag(imu_box_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					 LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(imu_box_obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
	lv_obj_add_event_cb(imu_box_obj, imu_box_draw, LV_EVENT_DRAW_TASK_ADDED, NULL);
	lv_obj_add_event_cb(imu_box_obj, imu_box_delete, LV_EVENT_DELETE, NULL);

	struct app_imu_sample sample;

	if (zbus_chan_read(&app_imu_sample_chan, &sample, K_NO_WAIT) == 0) {
		imu_box_project(sample.pitch, sample.roll);
	} else {
		imu_box_project(0.0f, 0.0f);
	}

	ui_section(content, "ORIENTATION");
	imu_pitch_label = lv_label_create(content);
	lv_obj_set_width(imu_pitch_label, LV_PCT(100));
	lv_obj_set_style_text_color(imu_pitch_label, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(imu_pitch_label, UI_FONT_BODY, 0);
	lv_label_set_text(imu_pitch_label, "Pitch --.- deg");

	imu_roll_label = lv_label_create(content);
	lv_obj_set_width(imu_roll_label, LV_PCT(100));
	lv_obj_set_style_text_color(imu_roll_label, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(imu_roll_label, UI_FONT_BODY, 0);
	lv_label_set_text(imu_roll_label, "Roll --.- deg");

	imu_gyro_label = lv_label_create(content);
	lv_obj_set_width(imu_gyro_label, LV_PCT(100));
	lv_obj_set_style_text_color(imu_gyro_label, lv_color_hex(0x929eaa), 0);
	lv_obj_set_style_text_font(imu_gyro_label, UI_FONT_CAPTION, 0);
	lv_label_set_text(imu_gyro_label, "Gyro -- -- -- dps");

	imu_box_timer_handle =
		lv_timer_create(imu_box_timer, CONFIG_OSKEY_IMU_SAMPLE_INTERVAL_MS, imu_box_obj);
}
#endif

static void show_splash(void)
{
	lv_obj_t *content = ui_page_begin("", UI_NAVIGATION_NONE);
	lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	ui_icon_color(ui_icon(content, &oskey_wallet_logo), ui_tone_color(UI_TONE_ACTIVE));
	lv_obj_t *name = lv_label_create(content);
	lv_obj_set_width(name, LV_PCT(100));
	lv_obj_set_style_text_color(name, lv_color_hex(0xf2f5f7), 0);
	lv_obj_set_style_text_font(name, UI_FONT_LARGE, 0);
	lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(name, "OSKey");
	lv_obj_add_flag(ui.status_bar, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
	lv_obj_align(content, LV_ALIGN_CENTER, 0, 0);
}

static void show_capabilities(void)
{
	static const char *const names[APP_FEATURE_COUNT] = {
		[APP_FEATURE_SECURE_BOOT] = "Secure boot",
		[APP_FEATURE_FLASH_ENCRYPTION] = "Flash encryption",
		[APP_FEATURE_BOOTLOADER] = "Bootloader",
		[APP_FEATURE_STORAGE] = "Storage",
		[APP_FEATURE_HARDWARE_RNG] = "Hardware RNG",
		[APP_FEATURE_DISPLAY_INPUT] = "Display and input",
		[APP_FEATURE_USER_BUTTON] = "User button",
	};

	ui_clear_sensitive();
	lv_obj_t *content = ui_page_begin("OSKey capabilities", UI_NAVIGATION_NONE);
	ui_section(content, "CAPABILITIES");
	for (size_t i = 0; i < ARRAY_SIZE(names); ++i) {
		bool enabled = ui.features[i];
		ui_list_row(content, enabled ? &oskey_success : &oskey_failure, names[i], NULL,
			    NULL, enabled ? UI_TONE_SUCCESS : UI_TONE_MUTED, NULL, NULL);
	}
	if (ui.status.wallet != WalletState_Disabled) {
		ui_section(content, "SETUP");
		ui_list_row(content, &oskey_wallet, "Set up OSKey", "Create or restore a wallet",
			    NULL, UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_PIN_NEW);
	}
}

static void show_home(void)
{
	lv_obj_t *content = ui_page_begin("OSKey", UI_NAVIGATION_NONE);
	ui_clear_sensitive();

	ui_list_row(content, &oskey_wallet, "Hardware wallet", "USB, Bluetooth or UART", NULL,
		    UI_TONE_ACTIVE, NULL, NULL);
#if defined(CONFIG_OSKEY_FIDO2)
	ui_list_row(content, &oskey_passkey, "Passkeys", "FIDO2 over USB", NULL, UI_TONE_ACTIVE,
		    NULL, NULL);
#endif
	ui_list_row(content, &oskey_settings, "Device settings", NULL, NULL, UI_TONE_ACTIVE,
		    navigate, (void *)(uintptr_t)UI_PAGE_SETTINGS);
}

static void show_settings(void)
{
	lv_obj_t *content = ui_page_begin("Device settings", UI_NAVIGATION_BACK);
	ui_clear_sensitive();
#if defined(CONFIG_OSKEY_WIFI) || defined(CONFIG_OSKEY_BLUETOOTH) || defined(CONFIG_OSKEY_USB)
	ui_section(content, "CONNECTIVITY");
#endif
#if defined(CONFIG_OSKEY_WIFI)
	ui_list_row(content, &oskey_wifi, "Wi-Fi", "Station, access point and saved network", NULL,
		    UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_WIFI);
#endif
#if defined(CONFIG_OSKEY_BLUETOOTH)
	ui_list_row(content, &oskey_bluetooth, "Bluetooth", "Wireless connection status", NULL,
		    UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_BLUETOOTH);
#endif
#if defined(CONFIG_OSKEY_USB)
	ui_list_row(content, &oskey_usb, "USB", "Host connection and available interfaces", NULL,
		    UI_TONE_ACTIVE, navigate, (void *)(uintptr_t)UI_PAGE_USB);
#endif
	ui_section(content, "MAINTENANCE");
	ui_list_row(content, &oskey_refresh, "Restart", "Restart without changing data", NULL,
		    UI_TONE_ACTIVE, confirm_restart, NULL);
	ui_list_row(content, &oskey_trash, "Erase wallet", "Remove wallet data permanently", NULL,
		    UI_TONE_DANGER, confirm_reset, NULL);
}

static void show_source(void)
{
	lv_obj_t *content = ui_page_begin("Create wallet", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_document, "Choose a recovery source",
		    "Generate a new phrase or restore one", NULL, UI_TONE_ACTIVE, NULL, NULL);
	ui_section(content, "RECOVERY SOURCE");
	ui_list_row(content, &oskey_wallet, "Generate recovery phrase",
		    ui.features[APP_FEATURE_HARDWARE_RNG] ? "Create with hardware randomness"
							  : "Test-only without hardware RNG",
		    NULL, UI_TONE_ACTIVE, generate_mnemonic, NULL);
	ui_list_row(content, &oskey_document, "Import recovery phrase",
		    "Restore an existing wallet", NULL, UI_TONE_ACTIVE, navigate,
		    (void *)(uintptr_t)UI_PAGE_IMPORT);
}

static void show_length(void)
{
	lv_obj_t *content = ui_page_begin(ui.custom_entropy ? "Custom entropy" : "Recovery phrase",
					  UI_NAVIGATION_BACK);
	ui_list_row(content, ui.custom_entropy ? &oskey_shuffle : &oskey_document,
		    ui.custom_entropy ? "Choose entropy size" : "Choose recovery length",
		    ui.custom_entropy ? "Every bit can be entered on screen"
				      : "Longer phrases provide more entropy",
		    NULL, UI_TONE_ACTIVE, NULL, NULL);
	if (ui.custom_entropy) {
		ui_section(content, "ENTROPY SIZE");
		ui_list_row(content, NULL, "12 words", "128-bit entropy", NULL, UI_TONE_DEFAULT,
			    select_entropy_size, (void *)(uintptr_t)128);
		ui_list_row(content, NULL, "24 words", "256-bit entropy", NULL, UI_TONE_DEFAULT,
			    select_entropy_size, (void *)(uintptr_t)256);
		return;
	}

	ui_section(content, "WORD COUNT");
	ui_list_row(content, NULL, "12 words", "128-bit entropy", NULL, UI_TONE_DEFAULT,
		    select_mnemonic_length, (void *)(uintptr_t)12);
	ui_list_row(content, NULL, "18 words", "192-bit entropy", NULL, UI_TONE_DEFAULT,
		    select_mnemonic_length, (void *)(uintptr_t)18);
	ui_list_row(content, NULL, "24 words", "256-bit entropy", NULL, UI_TONE_DEFAULT,
		    select_mnemonic_length, (void *)(uintptr_t)24);
	ui_section(content, "ADVANCED");
	ui_list_row(content, &oskey_shuffle, "Enter custom entropy", NULL, NULL, UI_TONE_ACTIVE,
		    enable_custom_entropy, NULL);
}

static void mnemonic_saved(lv_event_t *event)
{
	ARG_UNUSED(event);
	ui_push(UI_PAGE_VERIFY);
}

static void wipe_label(lv_event_t *event)
{
	const char *text = lv_label_get_text(lv_event_get_target_obj(event));

	if (text != NULL) {
		ui_wipe((void *)text, strlen(text));
	}
}

static void show_mnemonic(void)
{
	lv_obj_t *content = ui_page_begin("Recovery phrase", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_document, "Write these words down",
		    "Keep them offline and in order", NULL, UI_TONE_WARNING, NULL, NULL);
	ui_section(content, "RECOVERY WORDS");

	lv_obj_t *words = lv_obj_create(content);
	lv_obj_set_size(words, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(words, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(words, 0, 0);
	lv_obj_set_style_pad_all(words, 0, 0);
	lv_obj_set_style_pad_column(words, 10, 0);
	lv_obj_set_flex_flow(words, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_remove_flag(words, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
					  LV_OBJ_FLAG_SCROLLABLE);

	uint8_t index = 0;
	const char *word = ui.mnemonic;
	while (*word != '\0') {
		while (*word == ' ') {
			++word;
		}
		if (*word == '\0') {
			break;
		}
		const char *end = strchr(word, ' ');
		size_t len = end == NULL ? strlen(word) : (size_t)(end - word);
		char row[40];
		snprintf(row, sizeof(row), "%2u  %.*s", ++index, (int)len, word);

		lv_obj_t *label = lv_label_create(words);
		lv_obj_set_width(label, LV_PCT(48));
		lv_obj_set_height(label, 30);
		lv_obj_set_style_text_color(label, lv_color_hex(0xf2f5f7), 0);
		lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
		lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(label, lv_color_hex(0x242b33), 0);
		lv_obj_set_style_border_width(label, 1, 0);
		lv_obj_set_style_pad_ver(label, 5, 0);
		lv_label_set_text(label, row);
		lv_obj_add_event_cb(label, wipe_label, LV_EVENT_DELETE, NULL);
		ui_wipe(row, sizeof(row));
		word = end == NULL ? word + len : end + 1;
	}

	ui_section(content, "WHEN FINISHED");
	ui_list_row(content, &oskey_success, "I saved the recovery phrase",
		    "Continue to verification", NULL, UI_TONE_SUCCESS, mnemonic_saved, NULL);
}

static uint8_t entropy_columns(void)
{
	return ui.width < 360 ? 4 : 8;
}

static void entropy_click(lv_event_t *event)
{
	lv_obj_t *table = lv_event_get_target_obj(event);
	uint32_t row;
	uint32_t column;
	lv_table_get_selected_cell(table, &row, &column);
	uint8_t columns = entropy_columns();
	uint16_t bit = row * columns + column;
	if (bit >= ui.entropy_bits) {
		return;
	}
	ui.entropy[bit / 8] ^= BIT(7 - bit % 8);
	lv_table_set_cell_value(table, row, column,
				ui.entropy[bit / 8] & BIT(7 - bit % 8) ? "1" : "0");
}

static void entropy_finish(lv_event_t *event)
{
	ARG_UNUSED(event);
	uint8_t entropy[sizeof(ui.entropy)];
	size_t len = ui.entropy_bits / 8;
	memcpy(entropy, ui.entropy, len);
	ui_submit(LocalRequestKind_GenerateMnemonic, len / 4 * 3, entropy, len, NULL, 0);
	ui_wipe(entropy, sizeof(entropy));
}

static void show_entropy(void)
{
	uint8_t columns = entropy_columns();
	lv_obj_t *content = ui_page_begin("Custom entropy", UI_NAVIGATION_BACK);
	ui_list_row(content, &oskey_shuffle, "Set each entropy bit", "Tap a bit to toggle 0 or 1",
		    NULL, UI_TONE_ACTIVE, NULL, NULL);
	char section[16];
	snprintf(section, sizeof(section), "%u BITS", ui.entropy_bits);
	ui_section(content, section);

	lv_obj_t *table = lv_table_create(content);
	lv_obj_set_width(table, LV_PCT(100));
	lv_obj_set_height(table, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(table, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_ITEMS);
	lv_obj_set_style_border_color(table, lv_color_hex(0x303944), LV_PART_ITEMS);
	lv_obj_set_style_text_color(table, lv_color_hex(0xb8c1ca), LV_PART_ITEMS);
	lv_obj_set_style_text_font(table, UI_FONT_BODY, LV_PART_ITEMS);
	lv_obj_set_style_text_align(table, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);
	lv_obj_set_style_pad_ver(table, 15, LV_PART_ITEMS);
	lv_obj_set_style_pad_hor(table, 0, LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_border_color(table, lv_color_hex(0x4da3ff),
				      LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_text_color(table, lv_color_hex(0x4da3ff),
				    LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_table_set_row_count(table, DIV_ROUND_UP(ui.entropy_bits, columns));
	lv_table_set_column_count(table, columns);
	lv_obj_update_layout(content);
	int32_t column_width = lv_obj_get_content_width(content) / columns;
	for (uint8_t column = 0; column < columns; ++column) {
		lv_table_set_column_width(table, column, column_width);
	}
	for (uint16_t bit = 0; bit < ui.entropy_bits; ++bit) {
		lv_table_set_cell_value(table, bit / columns, bit % columns,
					ui.entropy[bit / 8] & BIT(7 - bit % 8) ? "1" : "0");
	}
	lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(table, entropy_click, LV_EVENT_VALUE_CHANGED, NULL);

	ui_section(content, "WHEN FINISHED");
	ui_list_row(content, &oskey_document, "Generate recovery phrase", "Use this entropy", NULL,
		    UI_TONE_ACTIVE, entropy_finish, NULL);
}

static void show_storage_error(void)
{
	lv_obj_t *content = ui_page_begin("Storage unavailable", UI_NAVIGATION_NONE);
	ui_list_row(content, &oskey_warning, "Secure storage could not be opened",
		    "Restart first; erase only if the problem continues", NULL, UI_TONE_WARNING,
		    NULL, NULL);
	ui_section(content, "RECOVERY ACTIONS");
	ui_list_row(content, &oskey_refresh, "Restart", "Try opening storage again", NULL,
		    UI_TONE_ACTIVE, confirm_restart, NULL);
	ui_list_row(content, &oskey_trash, "Erase storage", "Remove all device data", NULL,
		    UI_TONE_DANGER, confirm_reset, NULL);
}

void ui_render(void)
{
	switch (ui.page) {
	case UI_PAGE_SPLASH:
		show_splash();
		break;
	case UI_PAGE_CAPABILITIES:
		show_capabilities();
		break;
	case UI_PAGE_LOCKED:
	case UI_PAGE_PIN_NEW:
	case UI_PAGE_PIN_CONFIRM:
#if defined(CONFIG_OSKEY_FIDO2)
	case UI_PAGE_FIDO_PIN_RECOVER:
#endif
	case UI_PAGE_IMPORT:
	case UI_PAGE_VERIFY:
	case UI_PAGE_PASSPHRASE:
	case UI_PAGE_PASSPHRASE_CONFIRM:
		ui_input_page(&input_pages[ui.page], submit_current_input);
		break;
	case UI_PAGE_HOME:
		show_home();
		break;
	case UI_PAGE_CAMERA:
		ui_camera_render();
		break;
#if defined(CONFIG_OSKEY_QR_SCANNER)
	case UI_PAGE_QR_SCANNER:
		ui_qr_render();
		break;
#endif
	case UI_PAGE_SETTINGS:
		show_settings();
		break;
	case UI_PAGE_WIFI:
		ui_wifi_render();
		break;
	case UI_PAGE_WIFI_PASSWORD:
		ui_wifi_password_render();
		break;
	case UI_PAGE_BLUETOOTH:
		ui_bluetooth_render();
		break;
	case UI_PAGE_USB:
		ui_usb_render();
		break;
	case UI_PAGE_AUDIO:
		show_audio();
		break;
#if defined(CONFIG_OSKEY_IMU)
	case UI_PAGE_IMU:
		show_imu();
		break;
#endif
#if !defined(CONFIG_OSKEY_FIDO2)
	case UI_PAGE_FIDO_PIN_RECOVER:
		break;
#endif
	case UI_PAGE_SOURCE:
		show_source();
		break;
	case UI_PAGE_LENGTH:
		show_length();
		break;
	case UI_PAGE_MNEMONIC:
		show_mnemonic();
		break;
	case UI_PAGE_ENTROPY:
		show_entropy();
		break;
	case UI_PAGE_STORAGE_ERROR:
		show_storage_error();
		break;
	case UI_PAGE_CONFIRMATION:
		if (!ui_render_confirmation()) {
			ui.confirmation_id = 0;
			ui_back();
			return;
		}
		break;
	case UI_PAGE_NONE:
		break;
	}
	lv_obj_update_layout(ui.content);
	lv_obj_scroll_to(ui.content, 0, 0, LV_ANIM_OFF);
}

void ui_show_startup(void)
{
	if (ui.status.storage == APP_STORAGE_ERROR) {
		ui_open(UI_PAGE_STORAGE_ERROR);
		return;
	}

	switch (ui.status.wallet) {
	case WalletState_Locked:
		ui_open(UI_PAGE_LOCKED);
		break;
	case WalletState_Ready:
		ui_open(UI_PAGE_HOME);
		break;
	case WalletState_Setup:
	case WalletState_Busy:
	default:
		ui_open(UI_PAGE_CAPABILITIES);
		break;
	}
}

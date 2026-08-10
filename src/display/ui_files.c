#include "ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/util.h>

#include "assets/assets.h"
#include "sd_card.h"

#define FILES_PER_PAGE 16

static char current_path[SD_CARD_PATH_MAX] = SD_CARD_MOUNT_POINT;
static struct sd_card_entry entries[FILES_PER_PAGE];
static size_t page_offset;
static size_t entry_count;
static bool has_more;

static void retry(lv_event_t *event)
{
	ARG_UNUSED(event);
	(void)sd_card_unmount();
	snprintf(current_path, sizeof(current_path), "%s", SD_CARD_MOUNT_POINT);
	page_offset = 0;
	ui_refresh();
}

static void show_error(lv_obj_t *content, const char *title, const char *detail)
{
	ui_list_row(content, &oskey_warning, title, detail, NULL, UI_TONE_WARNING, NULL, NULL);
	ui_section(content, "ACTION");
	ui_list_row(content, &oskey_refresh, "Retry", "Mount and read the SD card again", NULL,
		    UI_TONE_ACTIVE, retry, NULL);
}

static void open_parent(lv_event_t *event)
{
	ARG_UNUSED(event);
	char *separator = strrchr(current_path, '/');
	if (separator == current_path) {
		snprintf(current_path, sizeof(current_path), "%s", SD_CARD_MOUNT_POINT);
	} else if (separator != NULL) {
		*separator = '\0';
	}
	page_offset = 0;
	ui_refresh();
}

static void open_directory(lv_event_t *event)
{
	const struct sd_card_entry *entry = lv_event_get_user_data(event);
	if (entry == NULL || !entry->directory) {
		return;
	}

	size_t length = strlen(current_path);
	int written =
		snprintf(current_path + length, sizeof(current_path) - length, "/%s", entry->name);
	if (written < 0 || (size_t)written >= sizeof(current_path) - length) {
		current_path[length] = '\0';
		ui_error("Directory path is too long");
		return;
	}
	page_offset = 0;
	ui_refresh();
}

static void previous_page(lv_event_t *event)
{
	ARG_UNUSED(event);
	page_offset = page_offset > FILES_PER_PAGE ? page_offset - FILES_PER_PAGE : 0;
	ui_refresh();
}

static void next_page(lv_event_t *event)
{
	ARG_UNUSED(event);
	page_offset += FILES_PER_PAGE;
	ui_refresh();
}

static void format_size(char *buffer, size_t length, size_t bytes)
{
	const uint64_t value = bytes;
	const uint64_t kib = 1024U;
	const uint64_t mib = kib * 1024U;
	const uint64_t gib = mib * 1024U;
	if (value >= gib) {
		snprintf(buffer, length, "%u.%u GiB", (unsigned int)(value / gib),
			 (unsigned int)((value % gib) * 10U / gib));
	} else if (value >= mib) {
		snprintf(buffer, length, "%u.%u MiB", (unsigned int)(value / mib),
			 (unsigned int)((value % mib) * 10U / mib));
	} else if (value >= kib) {
		snprintf(buffer, length, "%u.%u KiB", (unsigned int)(value / kib),
			 (unsigned int)((value % kib) * 10U / kib));
	} else {
		snprintf(buffer, length, "%u bytes", (unsigned int)bytes);
	}
}

void ui_files_render(void)
{
	lv_obj_t *content = ui_page_begin("Files", UI_NAVIGATION_BACK);
	int ret = sd_card_mount();
	if (ret < 0) {
		show_error(content, "SD card unavailable",
			   "Insert a FAT-formatted card, then retry");
		return;
	}

	ret = sd_card_list(current_path, page_offset, entries, ARRAY_SIZE(entries), &entry_count,
			   &has_more);
	if (ret == 0 && entry_count == 0 && page_offset > 0) {
		page_offset = 0;
		ret = sd_card_list(current_path, page_offset, entries, ARRAY_SIZE(entries),
				   &entry_count, &has_more);
	}
	if (ret < 0) {
		show_error(content, "Directory unavailable",
			   "The card may have been removed or its filesystem is unsupported");
		return;
	}

	const char *display_path = current_path + strlen(SD_CARD_MOUNT_POINT);
	ui_list_row(content, &oskey_document, display_path[0] == '\0' ? "/" : display_path,
		    "Read-only FAT filesystem", NULL, UI_TONE_MUTED, NULL, NULL);
	ui_section(content, "DIRECTORY");
	if (strcmp(current_path, SD_CARD_MOUNT_POINT) != 0) {
		ui_list_row(content, &oskey_back, "Parent directory", NULL, NULL, UI_TONE_ACTIVE,
			    open_parent, NULL);
	}

	for (size_t i = 0; i < entry_count; ++i) {
		if (entries[i].directory) {
			ui_list_row(content, &oskey_document, entries[i].name, "Folder", NULL,
				    UI_TONE_ACTIVE, open_directory, &entries[i]);
			continue;
		}

		char size[24];
		format_size(size, sizeof(size), entries[i].size);
		ui_list_row(content, &oskey_document, entries[i].name, size, NULL, UI_TONE_DEFAULT,
			    NULL, NULL);
	}

	if (entry_count == 0) {
		ui_list_row(content, &oskey_document, "Empty directory", "No files or folders",
			    NULL, UI_TONE_MUTED, NULL, NULL);
	}
	if (page_offset > 0 || has_more) {
		ui_section(content, "PAGES");
	}
	if (page_offset > 0) {
		ui_list_row(content, &oskey_back, "Previous page", NULL, NULL, UI_TONE_ACTIVE,
			    previous_page, NULL);
	}
	if (has_more) {
		ui_list_row(content, &oskey_chevron_right, "Next page", NULL, NULL, UI_TONE_ACTIVE,
			    next_page, NULL);
	}
}

void ui_files_leave(void)
{
	(void)sd_card_unmount();
	snprintf(current_path, sizeof(current_path), "%s", SD_CARD_MOUNT_POINT);
	page_offset = 0;
	entry_count = 0;
	has_more = false;
}

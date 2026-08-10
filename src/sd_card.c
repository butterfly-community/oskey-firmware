#include "sd_card.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(oskey_sd_card);

static FATFS fat_fs;
static bool mounted;
static struct fs_mount_t sd_mount = {
	.type = FS_FATFS,
	.mnt_point = SD_CARD_MOUNT_POINT,
	.fs_data = &fat_fs,
	.flags = FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_READ_ONLY | FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

int sd_card_mount(void)
{
	if (mounted) {
		return 0;
	}

	int ret = fs_mount(&sd_mount);
	if (ret < 0) {
		LOG_WRN("SD card mount failed: %d", ret);
		return ret;
	}

	mounted = true;
	return 0;
}

int sd_card_unmount(void)
{
	if (!mounted) {
		return 0;
	}

	int ret = fs_unmount(&sd_mount);
	if (ret < 0) {
		LOG_WRN("SD card unmount failed: %d", ret);
		return ret;
	}

	mounted = false;
	return 0;
}

int sd_card_list(const char *path, size_t offset, struct sd_card_entry *entries, size_t capacity,
		 size_t *count, bool *has_more)
{
	if (!mounted || path == NULL || entries == NULL || capacity == 0 || count == NULL ||
	    has_more == NULL ||
	    strncmp(path, SD_CARD_MOUNT_POINT, strlen(SD_CARD_MOUNT_POINT)) != 0) {
		return -EINVAL;
	}

	struct fs_dir_t directory;
	struct fs_dirent item;
	size_t skipped = 0;
	*count = 0;
	*has_more = false;
	fs_dir_t_init(&directory);

	int ret = fs_opendir(&directory, path);
	if (ret < 0) {
		return ret;
	}

	for (;;) {
		ret = fs_readdir(&directory, &item);
		if (ret < 0 || item.name[0] == '\0') {
			break;
		}
		if (strcmp(item.name, ".") == 0 || strcmp(item.name, "..") == 0) {
			continue;
		}
		if (skipped++ < offset) {
			continue;
		}
		if (*count == capacity) {
			*has_more = true;
			break;
		}

		struct sd_card_entry *entry = &entries[(*count)++];
		snprintf(entry->name, sizeof(entry->name), "%s", item.name);
		entry->size = item.size;
		entry->directory = item.type == FS_DIR_ENTRY_DIR;
	}

	int close_ret = fs_closedir(&directory);
	return ret < 0 ? ret : close_ret;
}

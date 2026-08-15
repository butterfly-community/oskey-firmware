/* SPDX-License-Identifier: MPL-2.0 */

#include "media.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(oskey_media);

static FATFS fat_fs;
static bool mounted;
static struct fs_mount_t mount_point = {
	.type = FS_FATFS,
	.mnt_point = MEDIA_MOUNT_POINT,
	.fs_data = &fat_fs,
	.flags = FS_MOUNT_FLAG_NO_FORMAT | FS_MOUNT_FLAG_READ_ONLY | FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

int media_mount(void)
{
	if (mounted) {
		return 0;
	}

	int ret = fs_mount(&mount_point);
	if (ret < 0) {
		LOG_WRN("Media mount failed: %d", ret);
		return ret;
	}

	mounted = true;
	return 0;
}

int media_unmount(void)
{
	if (!mounted) {
		return 0;
	}

	int ret = fs_unmount(&mount_point);
	if (ret < 0) {
		LOG_WRN("Media unmount failed: %d", ret);
		return ret;
	}

	mounted = false;
	return 0;
}

int media_list(const char *path, size_t offset, struct media_entry *entries, size_t capacity,
	       bool *has_more)
{
	if (!mounted || path == NULL || entries == NULL || capacity == 0 || has_more == NULL ||
	    strncmp(path, MEDIA_MOUNT_POINT, sizeof(MEDIA_MOUNT_POINT) - 1) != 0) {
		return -EINVAL;
	}

	struct fs_dir_t directory;
	struct fs_dirent item;
	size_t skipped = 0;
	size_t count = 0;
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
		if (count == capacity) {
			*has_more = true;
			break;
		}

		struct media_entry *entry = &entries[count++];
		snprintf(entry->name, sizeof(entry->name), "%s", item.name);
		entry->size = item.size;
		entry->directory = item.type == FS_DIR_ENTRY_DIR;
	}

	int close_ret = fs_closedir(&directory);
	if (ret < 0) {
		return ret;
	}
	return close_ret < 0 ? close_ret : (int)count;
}

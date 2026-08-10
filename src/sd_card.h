#ifndef OSKEY_SD_CARD_H
#define OSKEY_SD_CARD_H

#include <stdbool.h>
#include <stddef.h>

#define SD_CARD_MOUNT_POINT    "/SD:"
#define SD_CARD_PATH_MAX       256
#define SD_CARD_ENTRY_NAME_MAX 256

struct sd_card_entry {
	char name[SD_CARD_ENTRY_NAME_MAX];
	size_t size;
	bool directory;
};

int sd_card_mount(void);
int sd_card_unmount(void);
int sd_card_list(const char *path, size_t offset, struct sd_card_entry *entries, size_t capacity,
		 size_t *count, bool *has_more);

#endif

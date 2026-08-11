#ifndef OSKEY_MEDIA_H
#define OSKEY_MEDIA_H

#include <stdbool.h>
#include <stddef.h>

#define MEDIA_MOUNT_POINT    "/SD:"
#define MEDIA_PATH_MAX       256
#define MEDIA_ENTRY_NAME_MAX 256

struct media_entry {
	char name[MEDIA_ENTRY_NAME_MAX];
	size_t size;
	bool directory;
};

int media_mount(void);
int media_unmount(void);
int media_list(const char *path, size_t offset, struct media_entry *entries, size_t capacity,
	       bool *has_more);

#endif

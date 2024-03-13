#include "filesystem.h"

const char *fs_strfiletype(enum FS_INODETYPE type)
{
	char *FS_FILETYPE[] = {
		"empty", "file", "device", "directory"
	};

	return FS_FILETYPE[type];
}

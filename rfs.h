#ifndef RFS_H
#define RFS_H

#include "filesystem.h"

struct rfs_superblock {
	size_t			inodecnt;
	size_t			inodealloced;
	struct rfs_inode	*inodes;
	size_t			freeinode;
};

struct rfs_inode {
	size_t			nextfree;
	size_t			idx;
	size_t			size;
	size_t			allocsize;
	int			type;
	char *			data;
};

int rfs_getfs(struct filesystem *fs);

#endif

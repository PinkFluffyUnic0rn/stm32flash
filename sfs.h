#ifndef SFS_H
#define SFS_H

#include "filesystem.h"

#define SFS_PATHMAX 24

#define sfs_checksum_t uint32_t
#define sfs_size_t uint32_t
#define SFS_INODESECTORSCOUNT 15

struct sfs_superblock {
	sfs_checksum_t	checksum;
	sfs_size_t	inodecnt;
	sfs_size_t	inodesz;
	sfs_size_t	inodestart;
	sfs_size_t	freeinodes;
	sfs_size_t	blockstart;
	sfs_size_t	freeblocks;
	sfs_checksum_t	inodechecksum[SFS_INODESECTORSCOUNT + 1];
} __attribute__((packed));

struct sfs_allocedblocks {
	size_t block[2];
	size_t blockindirect;
} __attribute__((packed));

struct sfs_inode {
	sfs_checksum_t			checksum;
	sfs_size_t			nextfree;
	sfs_size_t			size;
	sfs_size_t			allocsize;
	uint32_t			type;
	struct sfs_allocedblocks	blocks;
} __attribute__((packed));

struct sfs_blockmeta {
	sfs_checksum_t	checksum;
	sfs_size_t	next;
	sfs_size_t	datasize;
} __attribute__((packed));

int sfs_getfs(struct filesystem *fs);

#endif

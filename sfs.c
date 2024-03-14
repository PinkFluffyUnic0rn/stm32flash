#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <string.h>

#include "vfs.h"

#include "sfs.h"

#define sfs_checksum_t uint32_t
#define fssize_t uint32_t

// values from underlying block device
#define SFS_WRITESIZE 256
#define SFS_SECTORSIZE 4096

#define SFS_INODESECTORSCOUNT 15
#define SFS_INODESSZ (SFS_SECTORSIZE * SFS_INODESECTORSCOUNT)
#define SFS_INODEPERSECTOR (SFS_SECTORSIZE / sizeof(struct sfs_inode))
#define SFS_RETRYCOUNT 5
#define SFS_DIRSIZE (SFS_SECTORSIZE - sizeof(struct sfs_blockmeta))
#define SFS_DIRRECORDSIZE 32
#define SFS_ROOTINODE 0x0001000
#define SFS_DATABLOCKSSTART (4096 * 16)
#define SFS_INITBLOCKSIZE 1024

#define sfs_blockgetmeta(b) (((struct sfs_blockmeta *) (b)))
#define sfs_checksumembed(buf, size) \
	sfs_checksum((char *) (buf) + sizeof(sfs_checksum_t), \
			(size) - sizeof(sfs_checksum_t))
#define sfs_checkdataembed(dev, addr, size, cs) \
	sfs_checkdata(dev, (addr) + sizeof(sfs_checksum_t), \
			(size) - sizeof(sfs_checksum_t), cs)
#define min(a, b) ((a) < (b) ? (a) : (b))

struct sfs_superblock {
	sfs_checksum_t	checksum;
	fssize_t	inodecnt;
	fssize_t	inodesz;
	fssize_t	inodestart;
	fssize_t	freeinodes;
	fssize_t	blockstart;
	fssize_t	freeblocks;
	sfs_checksum_t	inodechecksum[SFS_INODESECTORSCOUNT + 1];
} __attribute__((packed));

struct sfs_inode {
	sfs_checksum_t	checksum;
	fssize_t	nextfree;
	fssize_t	blocks;
	fssize_t	size;
	uint32_t	type;
	fssize_t	dummy0;
	fssize_t	dummy1;
	fssize_t	dummy2;
} __attribute__((packed));

struct sfs_blockmeta {
	sfs_checksum_t	checksum;
	fssize_t	next;
	fssize_t	datasize;
} __attribute__((packed));

const int Delay[] = {0, 10, 100, 1000, 5000};

int sfs_dircreate(struct device *dev, const char *path);

static int sfs_rewritesector(struct device *dev, size_t addr,
	const void *data, size_t sz)
{
	int i;

	dev->erasesector(dev->priv, addr);

	for (i = 0; i < sz; i += min(SFS_WRITESIZE, sz - i))
		dev->write(dev->priv, addr + i, data + i, SFS_WRITESIZE);

	return 0;
}

static sfs_checksum_t sfs_checksum(const void *buf, size_t size)
{
	sfs_checksum_t chk;
	size_t i;

	chk = 0;
	for (i = 0; i < size / sizeof(sfs_checksum_t); ++i)
		chk ^= ((sfs_checksum_t *) buf)[i];

	return chk;
}

static int sfs_checkdata(struct device *dev, size_t addr,
	size_t size, sfs_checksum_t cs)
{
	char buf[SFS_WRITESIZE];
	size_t i;
	sfs_checksum_t ccs;

	ccs = 0;
	for (i = 0; i < size; i += SFS_WRITESIZE) {
		size_t cursz;

		cursz = min(size - i, SFS_WRITESIZE);

		dev->read(dev->priv, addr + i, buf, cursz);

		ccs ^= sfs_checksum(buf, cursz);
	}

	return (ccs == cs);
}

static int sfs_readsuperblock(struct device *dev,
	struct sfs_superblock *sb)
{
	size_t sz;
	int i;

	sz = sizeof(struct sfs_superblock);

	for (i = 0; i < SFS_RETRYCOUNT; ++i) {
		dev->read(dev->priv, 0, sb, sz);

		if (sb->checksum == sfs_checksumembed(sb, sz))
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static int sfs_writesuperblock(struct device *dev,
	struct sfs_superblock *sb)
{
	size_t sz;
	int i;

	sz = sizeof(struct sfs_superblock);

	sb->checksum = sfs_checksumembed(sb, sz);

	for (i = 0; i < SFS_RETRYCOUNT; ++i) {
		struct sfs_superblock sbb;

		sfs_rewritesector(dev, 0, sb, sz);

		dev->read(dev->priv, 0, &sbb,
			sizeof(struct sfs_superblock));

		if (sbb.checksum == sfs_checksumembed(&sbb, sz))
			break;
		
		HAL_Delay(Delay[i]);
	}

	return 0;
}

static size_t sfs_readinode(struct device *dev, struct sfs_inode *in,
	size_t n, const struct sfs_superblock *sb)
{
	int i;
	size_t sz;

	sz = sizeof(struct sfs_inode);

	for (i = 0; i < SFS_RETRYCOUNT; ++i) {
		dev->read(dev->priv, n, in, sz);

		if (in->checksum == sfs_checksumembed(in, sz))
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static size_t sfs_writeinode(struct device *dev,
	const struct sfs_inode *in, size_t n,
	struct sfs_superblock *sb)
{
	struct sfs_inode buf[SFS_INODEPERSECTOR];
	size_t inodesector, inodesectorn, inodeid, sz;
	int i;

	sz = sizeof(struct sfs_inode);

	inodesector = n / SFS_SECTORSIZE * SFS_SECTORSIZE;
	inodesectorn = inodesector / SFS_SECTORSIZE;
	inodeid	= (n - inodesector) / sz;

	dev->read(dev->priv, inodesector, buf, SFS_SECTORSIZE);

	memmove(buf + inodeid, in, sz);

	buf[inodeid].checksum = sfs_checksumembed(buf + inodeid, sz);

	sb->inodechecksum[inodesectorn]
		= sfs_checksum(buf, SFS_SECTORSIZE);

	for (i = 0; i < SFS_RETRYCOUNT; ++i) {
		sfs_rewritesector(dev, inodesector, buf, SFS_SECTORSIZE);

		if (sfs_checkdata(dev, inodesector, SFS_SECTORSIZE,
			sb->inodechecksum[inodesectorn])) {
			break;
		}

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static size_t sfs_readdatablock(struct device *dev,
	size_t block, void *data)
{
	size_t totalsize;
	int i;

	for (i = 0; i < SFS_RETRYCOUNT; ++i) {
		sfs_checksum_t cs;

		dev->read(dev->priv, block, data, SFS_SECTORSIZE);

		totalsize = sizeof(struct sfs_blockmeta)
			+ sfs_blockgetmeta(data)->datasize;

		cs = sfs_checksumembed(data, totalsize);

		if (sfs_blockgetmeta(data)->checksum == cs)
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static size_t sfs_writedatablock(struct device *dev,
	size_t block, void *data)
{
	struct sfs_blockmeta *meta;
	size_t totalsize;
	int i;

	meta = sfs_blockgetmeta(data);

	totalsize = sizeof(struct sfs_blockmeta) + meta->datasize;

	meta->checksum = sfs_checksumembed(data, totalsize);

	for (i = 0; i < SFS_RETRYCOUNT; ++i) {
		sfs_checksum_t cs;

		sfs_rewritesector(dev, block, data, totalsize);

		dev->read(dev->priv, block, &cs, sizeof(sfs_checksum_t));

		if (sfs_checkdataembed(dev, block, totalsize, cs))
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static size_t sfs_createdatablock(struct device *dev,
	struct sfs_superblock *sb, size_t sz)
{
	size_t block, cursector, nextsector, restsz;
	struct sfs_blockmeta meta;

	if (sz == 0)
		sz = SFS_INITBLOCKSIZE;

	block = sb->freeblocks;

	restsz = sz + sz / SFS_SECTORSIZE * sizeof(meta);
	cursector = nextsector = sb->freeblocks;
	while (restsz > 0 && nextsector != 0) {
		char buf[SFS_SECTORSIZE];

		cursector = nextsector;

		restsz = (SFS_SECTORSIZE > restsz)
			? 0 : restsz - SFS_SECTORSIZE;

		sfs_readdatablock(dev, cursector, buf);

		nextsector = sfs_blockgetmeta(buf)->next;
	}

	if (nextsector == 0)
		return FS_ENODATABLOCKS;

	sb->freeblocks = nextsector;

	meta.next = 0;
	meta.datasize = 0;

	sfs_writedatablock(dev, cursector, &meta);

	return block;
}

static size_t sfs_deletedatablock(struct device *dev,
	size_t block, struct sfs_superblock *sb)
{
	size_t cursector, nextsector;
	struct sfs_blockmeta meta;

	if (block % SFS_SECTORSIZE || block < sb->blockstart)
		return FS_EWRONGADDR;

	cursector = nextsector = block;
	while (nextsector != 0) {
		char buf[SFS_SECTORSIZE];

		cursector = nextsector;

		sfs_readdatablock(dev, cursector, buf);

		nextsector = sfs_blockgetmeta(buf)->next;
	}

	if (cursector == 0)
		return FS_EBADDATABLOCK;

	meta.next = sb->freeblocks;
	meta.datasize = 0;

	sfs_writedatablock(dev, cursector, &meta);

	sb->freeblocks = block;

	return 0;
}

static size_t sfs_inoderesize(struct device *dev, size_t n, size_t sz)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t r;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	if (sz <= in.size)
		return 0;

	in.size = sz;

	r = sfs_deletedatablock(dev, in.blocks, &sb);
	if (fs_iserror(r))
		return r;

	in.blocks = sfs_createdatablock(dev, &sb, sz);
	if (fs_iserror(in.blocks))
		return in.blocks;

	sfs_writeinode(dev, &in, n, &sb);
	sfs_writesuperblock(dev, &sb);

	return 0;
}

static int sfs_writeinodeblock(struct device *dev,
	size_t inodesectorn, void *buf, struct sfs_superblock *sb)
{
	size_t inodesector;
	int i;

	inodesector = sb->inodestart + inodesectorn * SFS_SECTORSIZE;

	sb->inodechecksum[1 + inodesectorn]
		= sfs_checksum(buf, SFS_SECTORSIZE);

	dev->writesector(dev->priv, inodesector, buf, SFS_SECTORSIZE);

	for (i = 0; i < SFS_RETRYCOUNT; ++i) {
		if (sfs_checkdata(dev, inodesector, SFS_SECTORSIZE,
			sb->inodechecksum[1 + inodesectorn])) {
			break;
		}

		sfs_rewritesector(dev, inodesector, buf, SFS_SECTORSIZE);

		HAL_Delay(Delay[i]);
	}

	return 0;
}

size_t sfs_format(struct device *dev)
{
	struct sfs_superblock sb;
	struct sfs_inode buf[SFS_INODEPERSECTOR];
	size_t inodecnt, p, i;

	dev->eraseall(dev->priv);
	
	sb.inodecnt = SFS_INODESSZ / sizeof(struct sfs_inode);
	sb.inodesz = sizeof(struct sfs_inode);
	sb.inodestart = SFS_SECTORSIZE;
	sb.blockstart = SFS_DATABLOCKSSTART;
	sb.freeinodes = SFS_SECTORSIZE;
	sb.freeblocks = SFS_DATABLOCKSSTART;

	inodecnt = SFS_INODEPERSECTOR * SFS_INODESECTORSCOUNT;
	for (i = 0; i < inodecnt; ++i) {
		struct sfs_inode *curin;
		size_t insz;

		insz = sizeof(struct sfs_inode);

		curin = buf + i % SFS_INODEPERSECTOR;

		curin->nextfree = sb.inodestart + (i + 1) * sb.inodesz;
		curin->blocks = 0;
		curin->size = 0;
		curin->type = FS_EMPTY;

		curin->checksum = sfs_checksumembed(curin, insz);

		if ((i + 1) % SFS_INODEPERSECTOR == 0) {
			sfs_writeinodeblock(dev,
				i / SFS_INODEPERSECTOR, buf, &sb);
		}
	}

	sfs_writesuperblock(dev, &sb);

	for (p = sb.freeblocks; p < dev->totalsize; p += SFS_SECTORSIZE) {
		struct sfs_blockmeta meta;
		size_t sz;
		int j;

		sz = sizeof(struct sfs_blockmeta);

		meta.next = (p + SFS_SECTORSIZE >= dev->totalsize)
			? 0 : p + SFS_SECTORSIZE;
		meta.datasize = 0;

		meta.checksum = sfs_checksumembed(&meta, sz);

		dev->writesector(dev->priv, p, &meta, sz);

		for (j = 0; j < SFS_RETRYCOUNT; ++j) {
			sfs_checksum_t cs;

			dev->read(dev->priv, p, &cs,
				sizeof(sfs_checksum_t));

			if (sfs_checkdataembed(dev, p, sz, cs))
				break;

			sfs_rewritesector(dev, p, &meta, sz);

			HAL_Delay(Delay[j]);
		}
	}

	return 0;//sfs_dircreate(dev, "/");
}

size_t sfs_inodecreate(struct device *dev, size_t sz,
	enum FS_INODETYPE type)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t oldfree;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, sb.freeinodes, &sb);

	oldfree = sb.freeinodes;
	sb.freeinodes = in.nextfree;

	in.nextfree = 0;
	in.type = type;
	in.size = sz;

	in.blocks = sfs_createdatablock(dev, &sb, sz);
	if (fs_iserror(in.blocks))
		return in.blocks;

	sfs_writeinode(dev, &in, oldfree, &sb);
	sfs_writesuperblock(dev, &sb);

	return oldfree;
}

size_t sfs_inodedelete(struct device *dev, size_t n)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t r;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	if (n < sb.inodestart || n % sb.inodesz)
		return FS_EWRONGADDR;

	r = sfs_deletedatablock(dev, in.blocks, &sb);
	if (fs_iserror(r))
		return r;

	in.nextfree = sb.freeinodes;
	in.type = FS_EMPTY;
	in.size = 0;
	in.blocks = 0;

	sb.freeinodes = n;

	sfs_writeinode(dev, &in, n, &sb);
	sfs_writesuperblock(dev, &sb);

	return 0;
}

size_t sfs_inodeset(struct device *dev, size_t n,
	const void *data, size_t sz)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t block, step, r, bsz, p;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	if (n < sb.inodestart || n % sb.inodesz)
		return FS_EWRONGADDR;

	if (fs_iserror(r = sfs_inoderesize(dev, n, sz)))
		return r;

	bsz = sizeof(struct sfs_blockmeta);

	step = SFS_SECTORSIZE - bsz;
	block = in.blocks;
	for (p = 0; p < sz; p += step) {
		char sectorbuf[SFS_SECTORSIZE];
		struct sfs_blockmeta *meta;

		sfs_readdatablock(dev, block, sectorbuf);

		meta = sfs_blockgetmeta(sectorbuf);

		meta->datasize = min(sz - p, step);

		memmove(sectorbuf + bsz, data + p, meta->datasize);

		sfs_writedatablock(dev, block, sectorbuf);

		block = meta->next;
	}

	return 0;
}

size_t sfs_inodeget(struct device *dev, size_t n, void *data, size_t sz)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t block, step, bsz, p;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	if (n < sb.inodestart || n % sb.inodesz)
		return EWRONGADDR;

	if (sz < in.size)
		return FS_EWRONGSIZE;

	bsz = sizeof(struct sfs_blockmeta);

	step = SFS_SECTORSIZE - bsz;
	block = in.blocks;
	for (p = 0; p < in.size; p += step) {
		char sectorbuf[SFS_SECTORSIZE];

		sfs_readdatablock(dev, block, sectorbuf);

		memmove(data + p, sectorbuf + bsz, min(in.size - p, step));

		block = sfs_blockgetmeta(sectorbuf)->next;
	}

	return in.size;
}

static size_t sfs_splitpath(const char *path,
	const char **toks, size_t sz)
{
	static char pathbuf[SFS_DIRRECORDSIZE];
	int i;

	memmove(pathbuf, path, strlen(path) + 1);

	i = 0;

	toks[i++] = strtok(pathbuf, "/");

	while (i < sz && (toks[i++] = strtok(NULL, "/")) != NULL);

	--i;

	if (i >= sz)
		return FS_EPATHTOOLONG;

	if (toks[0] == NULL)
		return 0;

	return i;
}

static size_t sfs_dirfindinode(void *buf, size_t n)
{
	size_t offset;

	for (offset = 0; ; offset += SFS_DIRRECORDSIZE) {
		size_t nn;

		nn = 0xffffffff;

		memmove(&nn, buf + offset, sizeof(fssize_t));

		if (nn == n)
			return offset;
	}

	return FS_EINODENOTFOUND;
}

static size_t sfs_dirsearch(void *buf, const char *name)
{
	size_t offset;
	size_t sz;

	sz = sizeof(fssize_t);

	for (offset = 0; ; offset += SFS_DIRRECORDSIZE) {
		size_t n;

		n = 0xffffffff;

		memmove(&n, buf + offset, sz);

		if (n == 0xffffffff)
			return FS_ENAMENOTFOUND;

		if (strcmp((char *) buf + offset + sz, name) == 0)
			return n;
	}

	return FS_ENAMENOTFOUND;
}

size_t sfs_dirgetinode(struct device *dev, const char **path)
{
	struct sfs_superblock sb;
	char buf[SFS_DIRSIZE];
	size_t parn;
	const char **p;

	sfs_readsuperblock(dev, &sb);

	parn = SFS_ROOTINODE;

	for (p = path; *p != NULL; ++p) {
		struct sfs_inode in;

		sfs_readinode(dev, (&in), parn, &sb);

		if (in.type != FS_DIR)
			return FS_ENOTADIR;

		sfs_inodeget(dev, parn, buf, SFS_DIRSIZE);

		if (fs_iserror(parn = sfs_dirsearch(buf, *p)))
			return FS_ENAMENOTFOUND;
	}

	return parn;
}

size_t sfs_diradd(struct device *dev, size_t parn,
	const char *name, size_t n)
{
	char buf[SFS_DIRSIZE];
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t offset, r, b;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, parn, &sb);

	if (in.type != FS_DIR)
		return FS_ENOTADIR;

	r = sfs_inodeget(dev, parn, buf, SFS_DIRSIZE);
	if (fs_iserror(r))
		return r;

	offset = sfs_dirfindinode(buf, 0xffffffff);

	memmove(buf + offset, &n, sizeof(fssize_t));
	strcpy((char *) buf + offset + sizeof(fssize_t), name);

	b = 0xffffffff;
	memmove(buf + offset + SFS_DIRRECORDSIZE, &b, sizeof(fssize_t));

	if ((fs_iserror(parn = sfs_inodeset(dev, parn, buf,
			SFS_DIRSIZE)))) {
		return r;
	}

	return 0;
}

size_t sfs_dirdeleteinode(struct device *dev, size_t parn, size_t n)
{
	char buf[SFS_DIRSIZE];
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t offset, last, r, b;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, parn, &sb);

	if (in.type != FS_DIR)
		return FS_ENOTADIR;

	r = sfs_inodeget(dev, parn, buf, SFS_DIRSIZE);
	if (fs_iserror(r))
		return r;

	last = sfs_dirfindinode(buf, 0xffffffff);
	if (last == 0)
		return FS_ENAMENOTFOUND;

	if (last != SFS_DIRRECORDSIZE)
		last -= SFS_DIRRECORDSIZE;

	offset = sfs_dirfindinode(buf, n);

	memmove(buf + offset, buf + last, SFS_DIRRECORDSIZE);

	b = 0xffffffff;
	memmove(buf + last, &b, sizeof(fssize_t));

	sfs_inodeset(dev, parn, buf, SFS_DIRSIZE);

	return 0;
}

size_t sfs_dirisempty(struct device *dev, size_t n)
{
	char buf[SFS_DIRSIZE];
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t r, nn;
	
	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	if (in.type != FS_DIR)
		return 0;

	if (fs_iserror(r = sfs_inodeget(dev, n, buf, SFS_DIRSIZE)))
		return r;

	memmove(&nn, buf, sizeof(fssize_t));

	return ((nn != 0xffffffff) ? FS_EDIRNOTEMPTY : 0);
}

static int _sfs_dirlist(struct device *dev, size_t n,
	char *lbuf, size_t sz)
{
	char buf[SFS_DIRSIZE];
	size_t offset, r;

	r = sfs_inodeget(dev, n, buf, SFS_DIRSIZE);
	if (fs_iserror(r))	
		return fs_uint2interr(r);

	lbuf[0] = '\0';
	for (offset = 0; ; offset += SFS_DIRRECORDSIZE) {
		size_t nn;

		nn = 0xffffffff;

		memmove(&nn, buf + offset, sizeof(fssize_t));

		if (nn == 0xffffffff)
			break;

		strcat(lbuf, (char *) buf + offset + sizeof(fssize_t));
		strcat(lbuf, "\n\r");
	}

	return 0;
}

int sfs_dircreate(struct device *dev, const char *path)
{
	const char *toks[SFS_PATHMAX];
	const char *fname;
	size_t parn, n, b;
	int tokc;

	tokc = sfs_splitpath(path, toks, SFS_PATHMAX);

	if (tokc != 0 && sfs_dirgetinode(dev, toks)
			!= FS_ENAMENOTFOUND) {
		return EALREADYEXISTS;
	}

	n = sfs_inodecreate(dev, SFS_DIRSIZE, FS_DIR);
	if (fs_iserror(n))
		return fs_uint2interr(n);

	b = 0xffffffff;
	sfs_inodeset(dev, n, &b, sizeof(fssize_t));

	if (tokc == 0)
		return 0;

	fname = toks[tokc - 1];
	toks[tokc - 1] = NULL;

	if (fs_iserror(parn = sfs_dirgetinode(dev, toks)))
		return fs_uint2interr(parn);

	return fs_uint2interr(sfs_diradd(dev, parn, fname, n));
}

int sfs_dirdelete(struct device *dev, const char *path)
{
	const char *toks[SFS_PATHMAX];
	size_t parn, n;
	int tokc, r;

	tokc = sfs_splitpath(path, toks, SFS_PATHMAX);

	if (fs_iserror(n = sfs_dirgetinode(dev, toks)))
		return fs_uint2interr(n);

	if (fs_iserror(r = sfs_dirisempty(dev, n)))
		return fs_uint2interr(r);

	toks[tokc - 1] = NULL;

	if (fs_iserror(parn = sfs_dirgetinode(dev, toks)))
		return fs_uint2interr(parn);

	if (fs_iserror(r = sfs_dirdeleteinode(dev, parn, n)))
		return fs_uint2interr(r);

	if (fs_iserror(n = sfs_inodedelete(dev, n)))
		return fs_uint2interr(parn);

	return 0;
}

int sfs_dirlist(struct device *dev, const char *path,
	char *lbuf, size_t sz)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	const char *toks[SFS_PATHMAX];
	size_t n;

	sfs_splitpath(path, toks, SFS_PATHMAX);

	if (fs_iserror(n = sfs_dirgetinode(dev, toks)))
		return fs_uint2interr(n);

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	if (in.type != FS_DIR)
		return ENOTADIR;

	return _sfs_dirlist(dev, n, lbuf, sz);
}

int sfs_filewrite(struct device *dev, const char *path,
	const void *buf, size_t sz)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	const char *toks[SFS_PATHMAX];
	size_t n, r;

	sfs_splitpath(path, toks, SFS_PATHMAX);

	if (fs_iserror(n = sfs_dirgetinode(dev, toks)))
		return fs_uint2interr(n);

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	in.type = FS_FILE;
	in.size = sz;

	r = sfs_deletedatablock(dev, in.blocks, &sb);
	if (fs_iserror(r))
		return fs_uint2interr(r);

	in.blocks = sfs_createdatablock(dev, &sb, sz);
	if (fs_iserror(in.blocks))
		return fs_uint2interr(in.blocks);

	sfs_writeinode(dev, &in, n, &sb);
	sfs_writesuperblock(dev, &sb);

	if (fs_iserror(n = sfs_inodeset(dev, n, buf, sz)))
		return fs_uint2interr(n);

	return 0;
}

int sfs_fileread(struct device *dev, const char *path,
	void *buf, size_t sz)
{
	const char *toks[SFS_PATHMAX];
	struct sfs_superblock sb;
	struct sfs_inode in;
	size_t n;

	sfs_splitpath(path, toks, SFS_PATHMAX);

	if (fs_iserror(n = sfs_dirgetinode(dev, toks)))
		return fs_uint2interr(n);

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	if (in.type != FS_FILE)
		return ENOTAFILE;

	if (fs_iserror(n = sfs_inodeget(dev, n, buf, sz)))
		return fs_uint2interr(n);

	return 0;
}

int sfs_dirstat(struct device *dev, const char *path,
	struct fs_dirstat *st)
{
	struct sfs_superblock sb;
	struct sfs_inode in;
	const char *toks[SFS_PATHMAX];
	size_t n;
	int r;

	if ((r = sfs_splitpath(path, toks, SFS_PATHMAX)) < 0)
		return r;

	if (fs_iserror(n = sfs_dirgetinode(dev, toks)))
		return fs_uint2interr(n);

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	st->size = in.size;
	st->type = in.type;

	return 0;
}

int sfs_inodestat(struct device *dev, size_t n, struct fs_dirstat *st)
{
	struct sfs_superblock sb;
	struct sfs_inode in;

	sfs_readsuperblock(dev, &sb);
	sfs_readinode(dev, &in, n, &sb);

	st->size = in.size;
	st->type = in.type;

	return 0;
}

int sfs_getfs(struct filesystem *fs)
{
	fs->name = "sfs";
	fs->format = sfs_format;
	fs->inodecreate = sfs_inodecreate;
	fs->inodedelete = sfs_inodedelete;
	fs->inodeset = sfs_inodeset;
	fs->inodeget = sfs_inodeget;
	fs->inodestat = sfs_inodestat;
	
	fs->dircreate = sfs_dircreate;
	fs->dirlist = sfs_dirlist;
	fs->dirdelete = sfs_dirdelete;
	fs->filewrite = sfs_filewrite;
	fs->fileread = sfs_fileread;
	fs->dirstat = sfs_dirstat;
	fs->dirgetinode = sfs_dirgetinode;
	fs->rootinode = SFS_ROOTINODE;

	return 0;
}

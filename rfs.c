#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <string.h>

#include "rfs.h"
#include "calls.h"

#define RFS_ROOTINODE 0

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

static struct rfs_superblock sb;

size_t rfs_format(struct bdevice *dev)
{
	sb.inodecnt = 0;
	sb.inodealloced = 0;
	sb.inodes = NULL;
	sb.freeinode = 0xffffffff;

	return 0;
}

size_t rfs_inodecreate(struct bdevice *dev, size_t sz,
	enum FS_INODETYPE type)
{
	size_t idx;
	struct rfs_inode *in;

	if (sb.freeinode != 0xffffffff) {
		idx = sb.inodes[sb.freeinode].idx;

		sb.freeinode = sb.inodes[sb.freeinode].nextfree;
	} else {
		if (++sb.inodecnt > sb.inodealloced) {
			sb.inodealloced = sb.inodecnt * 2;
			sb.inodes = realloc(sb.inodes, sb.inodealloced
					* sizeof(struct rfs_inode));
		}

		idx = sb.inodecnt - 1;
	}
	
	in = sb.inodes + idx;

	in->nextfree = 0xffffffff;
	in->idx = idx;
	in->size = sz;
	in->allocsize = sz;
	in->type = type;

	if ((in->data = malloc(in->allocsize)) == NULL)
		return FS_EOUTOFMEMORY;

	return in->idx;
}

static size_t rfs_inoderesize(size_t n, size_t sz)
{
	struct rfs_inode *in;

	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	in = sb.inodes + n;
			
	if (sz > in->allocsize) {
		in->allocsize = sz * 2;
		if ((in->data = realloc(in->data, in->allocsize)) == NULL)
			return FS_EOUTOFMEMORY;
	}
	
	in->size = sz;

	return 0;
}

size_t rfs_inodedelete(struct bdevice *dev, size_t n)
{
	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	sb.inodes[n].nextfree = sb.freeinode;
	sb.freeinode = n;

	return 0;
}

size_t rfs_inodeset(struct bdevice *dev, size_t n,
	const void *data, size_t sz)
{
	size_t r;

	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	if (fs_iserror(r = rfs_inoderesize(n, sz)))
		return r;

	memcpy(sb.inodes[n].data, data, sz);

	return 0;
}

size_t rfs_inodeget(struct bdevice *dev, size_t n, void *data,
	size_t sz)
{
	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	if (sz < sb.inodes[n].size)
		return FS_EWRONGSIZE;

	memcpy(data, sb.inodes[n].data, sb.inodes[n].size);

	return sz;
}

size_t rfs_inoderead(struct bdevice *dev, size_t n, size_t offset,
	void *data, size_t sz)
{
	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	if (offset > sb.inodes[n].size)
		return 0;

	sz = min(sb.inodes[n].size - offset, sz);

	memcpy(data, sb.inodes[n].data + offset, sz);
	
	return sz;
}

size_t rfs_inodewrite(struct bdevice *dev, size_t n, size_t offset,
	const void *data, size_t sz)
{
	struct rfs_inode *in;
	size_t r;

	in = sb.inodes + n;
	
	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	if (fs_iserror(r = rfs_inoderesize(n, max(offset + sz, in->size))))
		return r;

	memcpy(sb.inodes[n].data + offset, data, sz);

	return 0;
}

size_t rfs_inodesettype(struct bdevice *dev, size_t n,
	enum FS_INODETYPE type)
{
	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	sb.inodes[n].type = type;

	return 0;
}

size_t rfs_inodestat(struct bdevice *dev, size_t n,
	struct fs_dirstat *st)
{
	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	st->size = sb.inodes[n].size;
	st->type = sb.inodes[n].type;

	return 0;
}

size_t rfs_dumpsuperblock(struct bdevice *dev, void *sba)
{
	memcpy(sba, &sb, sizeof(struct rfs_superblock));
	
	return 0;
}

size_t rfs_dumpinode(struct bdevice *dev, size_t n, void *in)
{
	if (n >= sb.inodecnt)
		return FS_EWRONGADDR;

	memcpy(in, sb.inodes + n, sizeof(struct rfs_inode));

	return 0;
}

size_t rfs_dumpblockmeta(struct bdevice *dev, size_t n, void *meta)
{
	return FS_ENOTALPLEMENTED;
}

int rfs_getfs(struct filesystem *fs)
{
	fs->name = "rfs";
	
	fs->dumpsuperblock = rfs_dumpsuperblock;
	fs->dumpinode = rfs_dumpinode;
	fs->dumpblockmeta = rfs_dumpblockmeta;

	fs->format = rfs_format;
	fs->inodecreate = rfs_inodecreate;
	fs->inodedelete = rfs_inodedelete;
	fs->inodeset = rfs_inodeset;
	fs->inodeget = rfs_inodeget;
	fs->inoderead = rfs_inoderead;
	fs->inodewrite = rfs_inodewrite;
	fs->inodestat = rfs_inodestat;
	fs->inodesettype = rfs_inodesettype;

	fs->rootinode = RFS_ROOTINODE;

	return 0;
}

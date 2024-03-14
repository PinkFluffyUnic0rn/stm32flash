#include "vfs.h"

#include <string.h>
#include <stdio.h>

struct vfsmount {
	struct device 			*dev;
	const char			*mountpoint[PATHMAXTOK];
	char				mountpointbuf[PATHMAX];
	const struct filesystem		*fs;
};

struct inode {
	struct vfsmount 	*mount;
	size_t 			addr;
};

struct file {
	char		path[PATHMAX];
	char		name[PATHMAX];
	int	 	flags;
	size_t		offset;
	struct inode	inode;
};

struct file files[FDMAX];
int fileset;

struct vfsmount mounts[MOUNTMAX];
int mountset;

char *pwd[PATHMAXTOK];
char *root[PATHMAXTOK];

static int splitpath(char *path, const char **toks, size_t sz)
{
	int i;

	if (strlen(path) > PATHMAX)
		return EPATHTOOBIG;

	i = 0;

	toks[i++] = strtok(path, "/");

	while (i < sz && (toks[i++] = strtok(NULL, "/")) != NULL);

	--i;

	if (i >= sz)
		return EPATHTOOLONG;

	if (toks[0] == NULL)
		return 0;

	return i;
}

static int allocinset(int *set)
{
	int i;

	for (i = 0; i < sizeof(int) * 8; ++i)
		if (((0x1 << i) & *set) == 0) {
			*set |= 0x1 << i;
			return i;
		}

	return (-1);
}

static int freefromset(int *set, int i)
{
	*set &= ~(0x1 << i);

	return 0;
}

static int isinset(int set, int i)
{
	return ((0x1 << i) & set);
}

static int pathcmp(const char **path0, const char **path1)
{
	const char **p0, **p1;
	
	p0 = path0;
	p1 = path1;
	while (*p0 != NULL && *p1 != NULL) {
		if (strcmp(*p0, *p1) != 0)
			return 0;
	
		p0++;
		p1++;
	}

	if (*p0 != NULL || *p1 != NULL)
		return 0;

	return 1;
}

static int findmountpoint(const char **toks)
{
	int i;

	for (i = 0; i < sizeof(int) * 8; ++i) {
		if (isinset(mountset, i)) {
			if (pathcmp(mounts[i].mountpoint, toks))
				return i;
		}
	}

	return EMOUNTNOTFOUND;
};

int init()
{
	mountset = fileset = 0;

	root[0] = "";
	pwd[0] = "";

	return 0;
}

int mount(struct device *dev, const char *target,
	const struct filesystem *fs)
{
	const char *toks[PATHMAXTOK];
	int mountid;
	int r;

	if ((mountid = allocinset(&mountset)) < 0)
		return EMOUNTSISFULL;

	strcpy(mounts[mountid].mountpointbuf, target);

	if ((r = splitpath(mounts[mountid].mountpointbuf,
			toks, PATHMAXTOK)) < 0) {
		freefromset(&mountset, mountid);
		return r;
	}

	mounts[mountid].dev = dev;
	mounts[mountid].fs = fs;

	memmove(mounts[mountid].mountpoint, toks,
		sizeof(char *) * PATHMAXTOK);

	return 0;
}

int umount(const char *target)
{
	int mountid;
	const char *toks[PATHMAXTOK];
	char pathbuf[PATHMAX];
	int r;

	strcpy(pathbuf, target);

	if ((r = splitpath(pathbuf, toks, PATHMAXTOK)) < 0)
		return r;

	if ((mountid = findmountpoint(toks)) < 0)
		return mountid;

	freefromset(&mountset, mountid);

	return 0;
}

int mountlist(const char **list, char *buf, size_t bufsz)
{
	size_t c;
	char *bufp;
	int i;

	c = 0;
	bufp = buf;
	for (i = 0; i < sizeof(int) * 8; ++i)
		if (isinset(mountset, i)) {
			const char **p;
			
			list[c++] = bufp;

			bufp[0] = '\0';
			for (p = mounts[i].mountpoint; *p != NULL; ++p)
				sprintf(bufp + strlen(bufp), "/%s", *p);
		
			bufp += strlen(bufp) + 1;
		}

	list[c] = NULL;

	return 0;
}

struct lookupres {
	const char *name;
	struct inode inode;
};

static size_t dirsearch(void *buf, const char *name)
{
	size_t offset;
	size_t sz;

	sz = sizeof(uint32_t);

	for (offset = 0; ; offset += DIRRECORDSIZE) {
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

static size_t dirlookup(const char **toks, struct lookupres *lr,
	int flags)
{
	const char *curpath[PATHMAXTOK];
	char dirbuf[DIRMAX];
	int mountid, curmountid, c;
	size_t n, rr;
	const char **p;

	c = 0;
	curpath[c] = NULL;
	n = 0;
	
	curmountid = findmountpoint((const char **) curpath);
	if (curmountid < 0) {
		if (curmountid != EMOUNTNOTFOUND)
			return ENOROOT;
	
		return curmountid;
	}
	
	n = mounts[curmountid].fs->rootinode;

	for (p = toks; *p != NULL; ++p) {
		struct fs_dirstat st;
		const struct filesystem *fs;
		struct device *dev;

		mountid = findmountpoint((const char **) curpath);
		if (mountid >= 0) {
			curmountid = mountid;
			n = mounts[curmountid].fs->rootinode;
		} else if (mountid != EMOUNTNOTFOUND)
			return mountid;

		fs = mounts[curmountid].fs;
		dev = mounts[curmountid].dev;

		fs->inodestat(dev, n, &st);

		if (st.type != FS_DIR)
			return ENOTADIR;

		rr = fs->inodeget(dev, n, dirbuf, DIRMAX);
		if (fs_iserror(rr))
			return fs_uint2interr(rr);

		n = dirsearch(dirbuf, *p);
		if (fs_iserror(n)) {
			if (flags & O_CREAT && *(p + 1) == NULL)
				return 1;
				
			return ENAMENOTFOUND;
		}

		curpath[c++] = *p;
		curpath[c] = NULL;
	}

	lr->name = (*p == NULL) ? "/" : *(p - 1);
	lr->inode.mount = mounts + curmountid;
	lr->inode.addr = n;

	return 0;
}

static size_t dirfindinode(void *buf, size_t n)
{
	size_t offset;

	for (offset = 0; ; offset += DIRRECORDSIZE) {
		size_t nn;

		nn = 0xffffffff;

		memmove(&nn, buf + offset, sizeof(uint32_t));

		if (nn == n)
			return offset;
	}

	return FS_EINODENOTFOUND;
}

static size_t diradd(struct inode *in, const char *name, size_t n)
{
	char buf[DIRMAX];
	struct fs_dirstat st;
	struct device *dev;
	const struct filesystem *fs;
	size_t offset, parn, r, b;

	dev = in->mount->dev;
	fs = in->mount->fs;
	parn = in->addr;

	fs->inodestat(dev, parn, &st);

	if (st.type != FS_DIR)
		return FS_ENOTADIR;

	if (fs_iserror(r = fs->inodeget(dev, parn, buf, DIRMAX)))
		return r;

	offset = dirfindinode(buf, 0xffffffff);

	memmove(buf + offset, &n, sizeof(uint32_t));
	strcpy((char *) buf + offset + sizeof(uint32_t), name);

	b = 0xffffffff;
	memmove(buf + offset + DIRRECORDSIZE, &b, sizeof(uint32_t));

	if ((fs_iserror(r = fs->inodeset(dev, parn, buf, DIRMAX))))
		return r;

	return 0;
}

size_t dirdeleteinode(struct inode *in, size_t n)
{
	char buf[DIRMAX];
	struct fs_dirstat st;
	struct device *dev;
	const struct filesystem *fs;
	size_t offset, last, parn, r, b;

	dev = in->mount->dev;
	fs = in->mount->fs;
	parn = in->addr;

	fs->inodestat(dev, parn, &st);

	if (st.type != FS_DIR)
		return FS_ENOTADIR;

	if (fs_iserror(r = fs->inodeget(dev, parn, buf, DIRMAX)))
		return r;

	last = dirfindinode(buf, 0xffffffff);
	if (last == 0)
		return FS_ENAMENOTFOUND;

	if (last != DIRRECORDSIZE)
		last -= DIRRECORDSIZE;

	offset = dirfindinode(buf, n);

	memmove(buf + offset, buf + last, DIRRECORDSIZE);

	b = 0xffffffff;
	memmove(buf + last, &b, sizeof(uint32_t));

	fs->inodeset(dev, parn, buf, DIRMAX);

	return 0;
}

static int mkfile(const char *path, enum FS_INODETYPE type)
{
	const char *toks[PATHMAXTOK];
	char pathbuf[PATHMAX];
	const char *name;
	struct lookupres lr;
	struct device *dev;
	const struct filesystem *fs;
	size_t n;
	int r, tokc;

	strcpy(pathbuf, path);

	if ((tokc = splitpath(pathbuf, toks, PATHMAXTOK)) < 0)
		return tokc;

	name = toks[tokc - 1];
	toks[tokc - 1] = NULL;

	if ((r = dirlookup(toks, &lr, 0)) < 0)
		return r;

	dev = lr.inode.mount->dev;
	fs = lr.inode.mount->fs;

	if (tokc == 0) {
		size_t b;
		
		n = fs->inodecreate(dev, FS_DIR, FS_DIR);
		if (fs_iserror(n))
			return fs_uint2interr(n);

		b = 0xffffffff;
		fs->inodeset(dev, n, &b, sizeof(uint32_t));

		return 0;
	}

	n = fs->inodecreate(dev, (type == FS_DIR) ? DIRMAX : 0, type);
	if (fs_iserror(n))
		return fs_uint2interr(n);

	if (type == FS_DIR) {
		size_t b;

		b = 0xffffffff;
		fs->inodeset(dev, n, &b, sizeof(uint32_t));
	}

	return fs_uint2interr(diradd(&(lr.inode), name, n));
}

int open(const char *path, int flags)
{
	const char *toks[PATHMAXTOK];
	char pathbuf[PATHMAX];
	struct lookupres lr;
	int fd, r;

	strcpy(pathbuf, path);

	if ((r = splitpath(pathbuf, toks, PATHMAXTOK)) < 0)
		return r;

	if ((r = dirlookup(toks, &lr, flags)) < 0)
		return r;

	if (r > 0) {
		if ((r = mkfile(path, FS_FILE)) < 0)
			return r;

		if ((r = dirlookup(toks, &lr, flags)) < 0)
			return r;
	}

	if ((fd = allocinset(&fileset)) < 0 || fd >= FDMAX)
		return ERUNOUTOFFD;

	strcpy(files[fd].path, path);
	strcpy(files[fd].name, lr.name);
	files[fd].flags = 0;
	files[fd].offset = 0;
	files[fd].inode = lr.inode;

	return fd;
}

int close(int fd)
{
	if (!isinset(fileset, fd))
		return EFDNOTSET;

	freefromset(&fileset, fd);

	return 0;
}

int write(int fd, const void *buf, size_t count)
{
	struct vfsmount *mnt;
	size_t r;

	if (!isinset(fileset, fd))
		return EFDNOTSET;

	mnt = files[fd].inode.mount;

	r = mnt->fs->inodeset(mnt->dev, files[fd].inode.addr,
		buf, count);
	if (fs_iserror(r))
		return fs_uint2interr(r);

	return 0;
}

int read(int fd, void *buf, size_t count)
{
	struct vfsmount *mnt;
	size_t r;

	if (!isinset(fileset, fd))
		return EFDNOTSET;

	mnt = files[fd].inode.mount;

	r = mnt->fs->inodeget(mnt->dev, files[fd].inode.addr,
		buf, count);
	if (fs_iserror(r))
		return fs_uint2interr(r);

	return 0;
}

int ioctl(int fd, int req, ...)
{
	if (!isinset(fileset, fd))
		return EFDNOTSET;


	return 0;
}

int lseek(int fd, size_t offset)
{
	if (!isinset(fileset, fd))
		return EFDNOTSET;

	files[fd].offset = offset;

	return 0;
}

static int dirisempty(struct inode *in)
{
	char buf[DIRMAX];
	struct fs_dirstat st;
	struct device *dev;
	const struct filesystem *fs;
	size_t r, n, nn;

	dev = in->mount->dev;
	fs = in->mount->fs;
	n = in->addr;

	fs->inodestat(dev, n, &st);

	if (st.type != FS_DIR)
		return 0;

	if (fs_iserror(r = fs->inodeget(dev, n, buf, DIRMAX)))
		return fs_uint2interr(r);

	memmove(&nn, buf, sizeof(uint32_t));

	return ((nn != 0xffffffff) ? EDIRNOTEMPTY : 0);
}

int unlink(const char *path)
{
	const char *toks[PATHMAXTOK];
	char pathbuf[PATHMAX];
	struct lookupres lr;
	struct device *dev;
	const struct filesystem *fs;
	size_t n, parn, rr;
	int r, tokc;

	if ((r = findmountpoint(toks)) != EMOUNTNOTFOUND) {
		if (r >= 0)
			return EISMOUNTPOINT;

		return r;
	}

	strcpy(pathbuf, path);

	if ((tokc = splitpath(pathbuf, toks, PATHMAXTOK)) < 0)
		return tokc;

	if ((r = dirlookup(toks, &lr, 0)) < 0)
		return r;
	
	dev = lr.inode.mount->dev;
	fs = lr.inode.mount->fs;
	n = lr.inode.addr;

	if ((r = dirisempty(&(lr.inode))) < 0)
		return r;

	toks[tokc - 1] = NULL;

	if ((r = dirlookup(toks, &lr, 0)) < 0)
		return r;

	parn = lr.inode.addr;
	
	if (fs_iserror(rr = dirdeleteinode(&(lr.inode), n)))
		return fs_uint2interr(rr);

	if (fs_iserror(n = fs->inodedelete(dev, n)))
		return fs_uint2interr(parn);

	return 0;
}

int mkdir(const char *path)
{
	return mkfile(path, FS_DIR);
}

int lsdir(const char *path, const char **list, char *buf, size_t bufsz)
{
	const char *toks[PATHMAXTOK];
	char dirbuf[DIRMAX], *bufp;
	char pathbuf[PATHMAX];
	struct lookupres lr;
	const struct filesystem *fs;
	struct device *dev;
	int r;
	size_t n, offset, rr, c;

	strcpy(pathbuf, path);

	if ((r = splitpath(pathbuf, toks, PATHMAXTOK)) < 0)
		return r;

	if ((r = dirlookup(toks, &lr, 0)) < 0)
		return r;

	dev = lr.inode.mount->dev;
	fs = lr.inode.mount->fs;
	n = lr.inode.addr;

	fs->inodeget(dev, n, dirbuf, DIRMAX);

	if (fs_iserror(rr = fs->inodeget(dev, n, dirbuf, DIRMAX)))
		return fs_uint2interr(rr);

	c = 0;
	bufp = buf;
	for (offset = 0; ; offset += DIRRECORDSIZE) {
		size_t nn;

		nn = 0xffffffff;

		memmove(&nn, dirbuf + offset, sizeof(uint32_t));

		if (nn == 0xffffffff)
			break;


		list[c++] = bufp;

		bufp[0] = '\0';
		
		strcat(bufp, (char *) dirbuf + offset + sizeof(uint32_t));
	
		bufp += strlen(bufp) + 1;
	}

	list[c] = NULL;

	return 0;
}

const char *vfs_strerror(enum ERROR e)
{
	char *strerror[] = {
		"success",
		"run out of data blocks",
		"wrong address",
		"bad data block",
		"wrong size",
		"path is too long",
		"inode not found",
		"path not found",
		"not a directory",
		"not a regular file",
		"directory is not empty",
		"directory already exists",
		"mount point not found",
		"path is too long",
		"maximum number of mount points reached",
		"root is not mounted",
		"run of of file descriptors",
		"file descriptor is not set",
		"directory is a mount point"
	};

	return strerror[-e];
}
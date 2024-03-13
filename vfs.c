#include "vfs.h"

#include <string.h>
#include <stdio.h>

struct vfsmount {
	struct device 	*dev;
	const char	*mountpoint[PATHMAXTOK];
	char		mountpointbuf[PATHMAX];
	const struct 	filesystem *fs;
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

int open(const char *path, int flags)
{	
	const char *toks[PATHMAXTOK];
	const char *curpath[PATHMAXTOK];
	char pathbuf[PATHMAX], dirbuf[FS_MAXDIR];
	int mountid, curmountid, fd, r, c;
	size_t parn, rr;
	const char **p;

	strcpy(pathbuf, path);

	if ((r = splitpath(pathbuf, toks, PATHMAXTOK)) < 0)
		return r;

	c = 0;
	curpath[c] = NULL;
	parn = 0;
	curmountid = -1;
	for (p = toks; *p != NULL; ++p) {
		struct fs_dirstat st;
		const struct filesystem *fs;
		struct device *dev;

		mountid = findmountpoint((const char **) curpath);
		if (mountid >= 0) {
			curmountid = mountid;
			parn = mounts[curmountid].fs->rootinode;
		} else if (mountid != EMOUNTNOTFOUND)
			return mountid;

		if (curmountid < 0)
			return ENOROOT;

		fs = mounts[curmountid].fs;
		dev = mounts[curmountid].dev;

		fs->inodestat(dev, parn, &st);

		if (st.type != FS_DIR)
			return ENOTADIR;

		rr = fs->inodeget(dev, parn, dirbuf, FS_MAXDIR);
		if (fs_iserror(rr))
			return fs_uint2interr(rr);

		parn = fs->dirsearch(dirbuf, *p);
		if (fs_iserror(parn))
			return ENAMENOTFOUND;

		curpath[c++] = *p;
		curpath[c] = NULL;
	}

	if ((fd = allocinset(&fileset)) < 0 || fd >= FDMAX)
		return ERUNOUTOFFD;

	strcpy(files[fd].path, path);
	strcpy(files[fd].name, *(p - 1));
	files[fd].flags = 0;
	files[fd].offset = 0;
	files[fd].inode.mount = mounts + curmountid;
	files[fd].inode.addr = parn;

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
		"file descriptor is not set"
	};

	return strerror[-e];
}

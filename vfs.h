#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#include "driver.h"
#include "filesystem.h"

#define PATHMAXTOK 24
#define PATHMAX 128
#define MOUNTMAX 32
#define FDMAX 32
#define ERRORMAX 0xff
#define DIRRECORDSIZE 32
#define DIRMAX (4096 - 64)

#define O_CREAT 0x1

enum ERROR {
	ESUCCESS	= 0x00,
	ENODATABLOCKS	= -0x01,
	EWRONGADDR	= -0x02,
	EBADDATABLOCK	= -0x03,
	EWRONGSIZE	= -0x04,
	EPATHTOOLONG	= -0x05,
	EINODENOTFOUND	= -0x06,
	ENAMENOTFOUND	= -0x07,
	ENOTADIR	= -0x08,
	ENOTAFILE	= -0x09,
	EDIRNOTEMPTY	= -0x0a,
	EALREADYEXISTS	= -0x0b,
	ESECTORTOOBIG	= -0x0c,
	EWRITETOOBIG	= -0x0d,
	EOUTOFMEMORY	= -0x0e,
	ENOTIMPLEMENTED	= -0x0f,
	EMOUNTNOTFOUND	= -0x10,
	EPATHTOOBIG	= -0x11,
	EMOUNTSISFULL	= -0x12,
	ENOROOT		= -0x13,
	ERUNOUTOFFD	= -0x14,
	EFDNOTSET	= -0x15,
	EISMOUNTPOINT	= -0x16,
	EWRONGPATH	= -0x17,
	EISADIR		= -0x18
};

int vfsinit();
int format(const char *target);
int mount(struct device *dev, const char *target,
	const struct filesystem *fs);
int umount(const char *target);
int mountlist(const char **list, char *buf, size_t bufsz);
int cd(const char *path);
int open(const char *path, int flags);
int close(int fd);
int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);
int ioctl(int fd, int req, ...);
int lseek(int fd, size_t offset);
int unlink(const char *path);
int mkdir(const char *path);
int lsdir(const char *path, const char **list, char *buf, size_t bufsz);
const char *vfs_strerror(enum ERROR e);

#endif

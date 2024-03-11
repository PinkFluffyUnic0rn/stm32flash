#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "driver.h"

#define fsaddr_t uint32_t
#define fssize_t uint32_t

#define FS_ENODATABLOCKS	0xffffff01
#define FS_EWRONGADDR		0xffffff02
#define FS_EBADDATABLOCK	0xffffff03
#define FS_EWRONGSIZE		0xffffff04
#define FS_EPATHTOOLONG		0xffffff05
#define FS_EINODENOTFOUND	0xffffff06
#define FS_ENAMENOTFOUND	0xffffff07
#define FS_ENOTADIR		0xffffff08
#define FS_ENOTAFILE		0xffffff09
#define FS_ENOTAFILE		0xffffff09
#define FS_EDIRNOTEMPTY		0xffffff0a
#define FS_EALREADYEXISTS	0xffffff0b

#define fs_uint2interr(v) (-((v) & 0xff))
#define fs_iserror(v) ((v) > 0xffffff00)

enum FS_INODETYPE {
	FS_EMPTY	= 0,
	FS_FILE		= 1,
	FS_DEV		= 2,
	FS_DIR		= 3
};

enum SFS_ERROR {
	SFS_ESUCCESS		= 0x00,
	SFS_ENODATABLOCKS	= -0x01,
	SFS_EWRONGADDR		= -0x02,
	SFS_EBADDATABLOCK	= -0x03,
	SFS_EWRONGSIZE		= -0x04,
	SFS_EPATHTOOLONG	= -0x05,
	SFS_EINODENOTFOUND	= -0x06,
	SFS_ENAMENOTFOUND	= -0x07,
	SFS_ENOTADIR		= -0x08,
	SFS_ENOTAFILE		= -0x09,
	SFS_EDIRNOTEMPTY	= -0x0a,
	SFS_EALREADYEXISTS	= -0x0b
};

struct fs_dirstat {
	fssize_t size;
	enum FS_INODETYPE type;
};

struct filesystem {
	const char *name;

	fsaddr_t (*checksum)(const void *buf, fsaddr_t size);
	fsaddr_t (*splitpath)(const char *path, const char **toks,
		size_t sz);

	fsaddr_t (*format)(struct device *dev);
	fsaddr_t (*inodecreate)(struct device *dev,
		fssize_t sz, enum FS_INODETYPE type);
	fsaddr_t (*inodedelete)(struct device *dev, fsaddr_t n);
	fsaddr_t (*inodeset)(struct device *dev, fsaddr_t n,
		const uint8_t *data, fssize_t sz);
	fsaddr_t (*inodeget)(struct device *dev, fsaddr_t n,
		uint8_t *data, fssize_t sz);
	fsaddr_t (*dirsearch)(uint8_t *buf, const char *name);
	fsaddr_t (*diradd)(struct device *dev, fsaddr_t parn,
		const char *name, fsaddr_t n);
	fsaddr_t (*dirdeleteinode)(struct device *dev,
		fsaddr_t parn, fsaddr_t n);

	int (*dircreate)(struct device *dev, const char *path);
	int (*dirlist)(struct device *dev, const char *path,
		char *lbuf, size_t sz);
	int (*dirdelete)(struct device *dev, const char *path);
	int (*filewrite)(struct device *dev, const char *path,
		const uint8_t *data, fsaddr_t sz);
	int (*fileread)(struct device *dev, const char *path,
		uint8_t *data, fsaddr_t sz);
	int (*dirstat)(struct device *dev, const char *path,
		struct fs_dirstat *st);
	fsaddr_t (*dirgetinode)(struct device *dev, const char **path);
	const char *(*strfiletype)(enum FS_INODETYPE type);
	const char *(*strerror)(enum SFS_ERROR e);
};

#endif

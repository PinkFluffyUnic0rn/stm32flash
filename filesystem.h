#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "driver.h"

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
#define FS_ESECTORTOOBIG	0xffffff0c
#define FS_EWRITETOOBIG		0xffffff0d

#define fs_uint2interr(v) (-((v) & 0xff))
#define fs_iserror(v) ((v) > 0xffffff00)

enum FS_INODETYPE {
	FS_EMPTY	= 0,
	FS_FILE		= 1,
	FS_DEV		= 2,
	FS_DIR		= 3
};

struct fs_dirstat {
	size_t			size;
	enum FS_INODETYPE	type;
};

struct filesystem {
	const char *name;

	size_t (*dumpsuperblock)(struct device *dev, void *sb);
	size_t (*dumpinode)(struct device *dev, size_t n, void *in);
	size_t (*dumpblockmeta)(struct device *dev,
		size_t n, void *meta);

	size_t (*format)(struct device *dev);
	size_t (*inodecreate)(struct device *dev,
		size_t sz, enum FS_INODETYPE type);
	size_t (*inodedelete)(struct device *dev, size_t n);
	size_t (*inodeset)(struct device *dev, size_t n,
		const void *data, size_t sz);
	size_t (*inodeget)(struct device *dev, size_t n,
		void *data, size_t sz);
	size_t (*inoderead)(struct device *dev, size_t n, size_t offset,
		void *data, size_t sz);
	size_t (*inodewrite)(struct device *dev, size_t n,
		size_t offset, const void *data, size_t sz);
	size_t (*inodesettype)(struct device *dev, size_t n,
		enum FS_INODETYPE type);
	size_t (*inodestat)(struct device *dev, size_t n,
		struct fs_dirstat *st);

	size_t rootinode;
};

const char *fs_strfiletype(enum FS_INODETYPE type);

#endif

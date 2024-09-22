#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>
#include <stddef.h>

#define DEVNAMEMAX 32

struct bdevice {
	char name[DEVNAMEMAX];
	int (*read)(void *dev, size_t addr, void *data, size_t sz);
	int (*write)(void *dev, size_t addr, const void *data,
		size_t sz);
	int (*ioctl)(void *dev, int req, ...);

	int (*eraseall)(void *dev);
	int (*erasesector)(void *dev, size_t addr);
	int (*writesector)(void *dev, size_t addr, const void *data,
		size_t sz);

	size_t writesize;
	size_t sectorsize;
	size_t totalsize;

	void *priv;
};

struct driver {
	int (*initdevice)(void *, struct bdevice *dev);
};

#endif

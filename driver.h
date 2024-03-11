#ifndef DRIVER_H
#define DRIVER_H

enum DEVTYPE {
	DEVTYPE_CHAR = 0,
	DEVTYPE_BLOCK = 1
};

struct device {
	enum DEVTYPE type;
	int (*read)(void *dev, uint32_t addr, uint8_t *data, size_t sz);
	int (*write)(void *dev, uint32_t addr, const uint8_t *data,
		size_t sz);

	int (*eraseall)(void *dev);
	int (*erasesector)(void *dev, uint32_t addr);
	int (*writesector)(void *dev, uint32_t addr, const uint8_t *data,
		size_t sz);

	size_t writesize;
	size_t sectorsize;
	size_t totalsize;

	void *priv;
};

struct driver {
	int (*initdevice)(void *, struct device *dev);
};

#endif

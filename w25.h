#ifndef W25_H
#define W25_H

#define W25_PAGESIZE 256
#define W25_SECTORSIZE 4096
#define W25_BLOCKSIZE (4096 * 16)
#define W25_TOTALSIZE (1024 * 1024 * 16)

#define W25FS_PATHMAX 24

enum W25FS_ERROR {
	W25FS_ESUCCESS		= 0x00,
	W25FS_ENODATABLOCKS	= -0x01,
	W25FS_EWRONGADDR	= -0x02,
	W25FS_EBADDATABLOCK	= -0x03,
	W25FS_EWRONGSIZE	= -0x04,
	W25FS_EPATHTOOLONG	= -0x05,
	W25FS_EINODENOTFOUND	= -0x06,
	W25FS_ENAMENOTFOUND	= -0x07,
	W25FS_ENOTADIR		= -0x08,
	W25FS_ENOTAFILE		= -0x09,
	W25FS_EDIRNOTEMPTY	= -0x0a,
	W25FS_EALREADYEXISTS	= -0x0b
};

enum W25FS_INODETYPE {
	W25FS_EMPTY	= 0,
	W25FS_FILE	= 1,
	W25FS_DEV	= 2,
	W25FS_DIR	= 3
};

struct w25fs_device {
	SPI_HandleTypeDef *hspi;
	GPIO_TypeDef *gpio;
	uint16_t pin;
};

struct w25fs_dirstat {
	uint32_t size;
	enum W25FS_INODETYPE type;
};

int w25fs_initdevice(struct w25fs_device *dev, SPI_HandleTypeDef *hspi,
	GPIO_TypeDef *gpio, uint16_t pin);

int w25_read(struct w25fs_device *dev, uint32_t addr, uint8_t *data,
	uint32_t sz);

int w25_write(struct w25fs_device *dev, uint32_t addr,
	const uint8_t *data, uint32_t sz);

int w25_eraseall(struct w25fs_device *dev);

int w25_erasesector(struct w25fs_device *dev, uint32_t n);

int w25_eraseblock(struct w25fs_device *dev, uint32_t n);

uint32_t w25fs_format(struct w25fs_device *dev);

uint32_t w25fs_inodecreate(struct w25fs_device *dev,
	uint32_t sz, enum W25FS_INODETYPE type);

uint32_t w25fs_inodedelete(struct w25fs_device *dev, uint32_t n);

uint32_t w25fs_inodeset(struct w25fs_device *dev, uint32_t n,
	const uint8_t *data, uint32_t sz);

uint32_t w25fs_inodeget(struct w25fs_device *dev, uint32_t n,
	uint8_t *data, uint32_t sz);

uint32_t w25fs_checksum(const void *buf, uint32_t size);

int w25fs_dircreate(struct w25fs_device *dev, const char *path);

int w25fs_dirlist(struct w25fs_device *dev, const char *path,
	char *lbuf, size_t sz);

int w25fs_dirdelete(struct w25fs_device *dev, const char *path);

int w25fs_filewrite(struct w25fs_device *dev, const char *path,
	const uint8_t *data, uint32_t sz);

int w25fs_fileread(struct w25fs_device *dev, const char *path,
	uint8_t *data, uint32_t sz);

int w25fs_dirstat(struct w25fs_device *dev, const char *path,
	struct w25fs_dirstat *st);

uint32_t w25fs_splitpath(const char *path, const char **toks, size_t sz);

uint32_t w25fs_dirgetinode(struct w25fs_device *dev, const char **path);

const char *w25fs_strfiletype(enum W25FS_INODETYPE type);

const char *w25fs_strerror(enum W25FS_ERROR e);

#endif

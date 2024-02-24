#ifndef W25_H
#define W25_H

#define W25_PAGESIZE 256
#define W25_SECTORSIZE 4096
#define W25_BLOCKSIZE (4096 * 16)
#define W25_TOTALSIZE (1024 * 1024 * 16)

#define W25FS_NODATABLOCKS 0xffffff01
#define W25FS_WRONGADDR 0xffffff02
#define W25FS_BADDATABLOCK 0xffffff03
#define W25FS_WRONGSIZE 0xffffff04

#define w25fs_iserror(v) ((v) > 0xffffff00)

enum W25FS_INODETYPE {
	W25FS_EMPTY = 0,
	W25FS_FILE = 1,
	W25FS_DEV = 2,
	W25FS_DIR = 3
};

struct w25fs_dirstat {
	uint32_t size;
	enum W25FS_INODETYPE type;
};

int w25_init();

int w25_getid();

int w25_read(uint32_t addr, uint8_t *data, uint32_t sz);

int w25_write(uint32_t addr, uint8_t *data, uint32_t sz);

int w25_eraseall();

int w25_erasesector(uint32_t n);

int w25_eraseblock(uint32_t n);

uint32_t w25fs_format();

uint32_t w25fs_inodecreate(uint32_t sz, enum W25FS_INODETYPE type);

uint32_t w25fs_inodedelete(uint32_t n);

uint32_t w25fs_inodeset(uint32_t n, uint8_t *data, uint32_t sz);

uint32_t w25fs_inodeget(uint32_t n, uint8_t *data, uint32_t sz);

int w25fs_dircreate(const char *path);

int w25fs_dirlist(const char *path, char *lbuf, size_t sz);

int w25fs_dirdelete(const char *path);

int w25fs_filewrite(const char *path, const char *data, size_t sz);

int w25fs_fileread(const char *path, char *data, size_t sz);

int w25fs_dirstat(const char *path, struct w25fs_dirstat *st);

int w25fs_splitpath(const char *path, char **toks, size_t sz);

uint32_t w25fs_dirgetinode(const char **path);

char *w25fs_filetype(enum W25FS_INODETYPE type);

#endif

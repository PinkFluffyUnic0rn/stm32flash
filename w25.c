#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <string.h>

#include "w25.h"

#define W25FS_INODESSZ (W25_SECTORSIZE * 15)
#define W25FS_INODEPERSECTOR (W25_SECTORSIZE / sizeof(struct w25fs_inode))

#define W25FS_DIRRECORDSIZE 32
#define W25FS_ROOTINODE 0x0001000

struct w25fs_superblock {
	uint32_t inodecnt;
	uint32_t inodesz;
	uint32_t inodestart;
	uint32_t freeinodes;
	uint32_t blockstart;
	uint32_t freeblocks;
} __attribute__((packed));

struct w25fs_inode {
	uint32_t nextfree;
	uint32_t blocks;
	uint32_t size;
	uint32_t type;
} __attribute__((packed));

static uint8_t Sbuf[4];
static uint8_t Rbuf[4];

SPI_HandleTypeDef *Hspi;

int w25_init(SPI_HandleTypeDef *hs)
{
	Hspi = hs;

	HAL_Delay(100);

	Sbuf[0] = 0x66;
	Sbuf[1] = 0x99;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 2, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
	
	HAL_Delay(100);
	
	return 0;
}

int w25_getid()
{
	Sbuf[0] = 0x9f;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 1, 5000);
	HAL_SPI_Receive(Hspi, Rbuf, 3, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

	return ((Rbuf[0] << 16) | (Rbuf[1] << 8) | Rbuf[2]);
}

int w25_read(uint32_t addr, uint8_t *data, uint32_t sz)
{
	Sbuf[0] = 0x03;
	Sbuf[1] = (addr >> 16) & 0xff;
	Sbuf[2] = (addr >> 8) & 0xff;
	Sbuf[3] = addr & 0xff;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 4, 5000);
	HAL_SPI_Receive(Hspi, data, sz, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
	
	return 0;
}

static int w25_writeenable()
{
	Sbuf[0] = 0x06;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 1, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
	
	return 0;
}

static int w25_writedisable()
{
	Sbuf[0] = 0x04;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 1, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
	
	return 0;
}

static int w25_waitwrite()
{
	Sbuf[0] = 0x05;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

	HAL_SPI_Transmit(Hspi, Sbuf, 1, 5000);

	do {
		HAL_SPI_Receive(Hspi, Rbuf, 1, 5000);
	} while ((Rbuf[0] & 0x01) == 0x01);

	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
	
	return 0;
}

static int w25_blockprotect(uint8_t flags)
{
	Sbuf[0] = 0x50;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 1, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

	Sbuf[0] = 0x01;
	Sbuf[1] = (flags & 0x0f) << 2;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 2, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

	return 0;
}

int w25_write(uint32_t addr, uint8_t *data, uint32_t sz)
{
	w25_waitwrite();

	w25_blockprotect(0x00);
	w25_writeenable();

	Sbuf[0] = 0x02;
	Sbuf[1] = (addr >> 16) & 0xff;
	Sbuf[2] = (addr >> 8) & 0xff;
	Sbuf[3] = addr & 0xff;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 4, 5000);
	HAL_SPI_Transmit(Hspi, data, sz, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
	
	w25_waitwrite();
	w25_writedisable();
	w25_blockprotect(0x0f);
	
	return 0;
}

int w25_writesector(uint32_t addr, uint8_t *data)
{
	int i;

	for (i = 0; i < W25_SECTORSIZE; i += W25_PAGESIZE)
		w25_write(addr + i, data + i, W25_PAGESIZE);

	return 0;
}

int w25_eraseall()
{
	w25_waitwrite();
	w25_blockprotect(0x00);
	w25_writeenable();
	
	Sbuf[0] = 0xc7;
	
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 1, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

	w25_waitwrite();
	w25_writedisable();
	w25_blockprotect(0x0f);
	
	return 0;
}

int w25_erasesector(uint32_t addr)
{
	w25_waitwrite();
	w25_blockprotect(0x00);
	w25_writeenable();

	Sbuf[0] = 0x20;
	Sbuf[1] = (addr >> 16) & 0xff;
	Sbuf[2] = (addr >> 8) & 0xff;
	Sbuf[3] = addr & 0xff;

	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 4, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

	w25_waitwrite();
	w25_writedisable();
	w25_blockprotect(0x0f);

	return 0;
}

int w25_eraseblock(uint32_t n)
{
	uint32_t addr;

	w25_waitwrite();
	w25_blockprotect(0x00);
	w25_writeenable();

	addr = n * W25_BLOCKSIZE;

	Sbuf[0] = 0xD8;
	Sbuf[1] = (addr >> 16) & 0xff;
	Sbuf[2] = (addr >> 8) & 0xff;
	Sbuf[3] = addr & 0xff;

	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
	HAL_SPI_Transmit(Hspi, Sbuf, 4, 5000);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);

	w25_waitwrite();
	w25_writedisable();
	w25_blockprotect(0x0f);

	return 0;
}

static int w25fs_readsuperblock(struct w25fs_superblock *sb)
{
	w25_read(0, (uint8_t *) (sb), sizeof(struct w25fs_superblock));

	return 0;
}

static int w25fs_writesuperblock(const struct w25fs_superblock *sb)
{
	w25_erasesector(0);
	w25_write(0, (uint8_t *) sb,
		sizeof(struct w25fs_superblock));

	return 0;
}

static uint32_t w25fs_readinode(struct w25fs_inode *in, uint32_t n)
{
	w25_read(n, (uint8_t *) (in), sizeof(struct w25fs_inode));

	return 0;
}

static uint32_t w25fs_writeinode(const struct w25fs_inode *in,
	uint32_t n)
{
	struct w25fs_inode buf[W25FS_INODEPERSECTOR];
	uint32_t inodesector, inodeid;

	inodesector = n / W25_SECTORSIZE * W25_SECTORSIZE;
	inodeid	= (n - inodesector) / sizeof(struct w25fs_inode);

	w25_read(inodesector, (uint8_t *) buf, W25_SECTORSIZE);

	memmove(buf + inodeid, in, sizeof(struct w25fs_inode));

	w25_erasesector(inodesector);
	w25_writesector(inodesector, (uint8_t *) buf);

	return 0;
}

static uint32_t w25fs_createdatablock(struct w25fs_superblock *sb,
	uint32_t sz)
{
	uint32_t block, cursector, nextsector, restsz;
	uint32_t b;
	
	block = sb->freeblocks;

	restsz = sz + sz / W25_SECTORSIZE * 4;
	cursector = nextsector = sb->freeblocks;
	while (restsz > 0 && nextsector != 0) {
		cursector = nextsector;

		restsz = (W25_SECTORSIZE > restsz)
			? 0 : restsz - W25_SECTORSIZE;

		w25_read(cursector, (uint8_t *) (&nextsector), 4);
	}

	if (nextsector == 0)
		return W25FS_NODATABLOCKS;

	sb->freeblocks = nextsector;

	b = 0;
	w25_erasesector(cursector);
	w25_write(cursector, (uint8_t *) (&b), 4);

	return block;
}

static uint32_t w25fs_deletedatablock(uint32_t block,
	struct w25fs_superblock *sb)
{
	uint32_t cursector, nextsector;
	uint32_t b;
	
	if (block % W25_SECTORSIZE || block < sb->blockstart)
		return W25FS_WRONGADDR;

	cursector = nextsector = block;
	while (nextsector != 0) {
		cursector = nextsector;

		w25_read(cursector, (uint8_t *) (&nextsector), 4);
	}

	if (cursector == 0)
		return W25FS_BADDATABLOCK;

	b = sb->freeblocks;
	w25_erasesector(cursector);
	w25_write(cursector, (uint8_t *) (&b), 4);
	
	sb->freeblocks = block;

	return 0;
}

uint32_t w25fs_format()
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint8_t buf[W25_PAGESIZE];
	uint32_t p;

	sb.inodecnt = W25FS_INODESSZ / sizeof(struct w25fs_inode);
	sb.inodesz = sizeof(struct w25fs_inode);
	sb.inodestart = W25_SECTORSIZE;
	sb.blockstart = W25_BLOCKSIZE;
	sb.freeinodes = W25_SECTORSIZE;
	sb.freeblocks = W25_BLOCKSIZE;

	w25_eraseall();
	
	w25_write(0, (uint8_t *) (&sb), sizeof(sb));

	in.blocks = 0;
	in.size = 0;
	in.type = W25FS_EMPTY;

	for (p = 0; p < sb.inodecnt * sb.inodesz; p += sb.inodesz) {
		in.nextfree = sb.inodestart + p + sb.inodesz;

		memmove(buf + (p % W25_PAGESIZE), &in,
			sizeof(struct w25fs_inode));

		if (((p + sb.inodesz) % W25_PAGESIZE) == 0) {
			w25_write(sb.inodestart + p / W25_PAGESIZE * W25_PAGESIZE,
				buf, W25_PAGESIZE);
		}
	}

	for (p = W25_BLOCKSIZE; p < W25_TOTALSIZE; p += W25_SECTORSIZE) {
		uint32_t next;

		next = (p + W25_SECTORSIZE >= W25_TOTALSIZE)
			? 0 : p + W25_SECTORSIZE;

		w25_write(p, (uint8_t *) (&next), 4);
	}

	return 0;
}

uint32_t w25fs_inodecreate(uint32_t sz, enum W25FS_INODETYPE type)
{
	struct w25fs_inode buf[W25FS_INODEPERSECTOR];
	uint32_t inodesector, inodeid;
	struct w25fs_superblock sb;

	w25fs_readsuperblock(&sb);

	inodesector = sb.freeinodes / W25_SECTORSIZE * W25_SECTORSIZE;
	inodeid	= (sb.freeinodes - inodesector) / sizeof(struct w25fs_inode);

	w25_read(inodesector, (uint8_t *) buf, W25_SECTORSIZE);

	sb.freeinodes = buf[inodeid].nextfree;

	buf[inodeid].nextfree = 0;
	buf[inodeid].type = type;
	buf[inodeid].size = sz;
	
	buf[inodeid].blocks = w25fs_createdatablock(&sb, sz);
	if (w25fs_iserror(buf[inodeid].blocks))
		return buf[inodeid].blocks;

	w25_erasesector(inodesector);
	w25_writesector(inodesector, (uint8_t *) buf);

	w25fs_writesuperblock(&sb);

	return (inodesector + inodeid * sizeof(struct w25fs_inode));
}

uint32_t w25fs_inodedelete(uint32_t n)
{
	struct w25fs_inode buf[W25FS_INODEPERSECTOR];
	struct w25fs_superblock sb;
	uint32_t inodesector, inodeid;
	uint32_t r;

	w25fs_readsuperblock(&sb);

	if (n < sb.inodestart || n % sb.inodesz)
		return W25FS_WRONGADDR;

	inodesector = n / W25_SECTORSIZE * W25_SECTORSIZE;
	inodeid	= (n - inodesector) / sizeof(struct w25fs_inode);

	w25_read(inodesector, (uint8_t *) buf, W25_SECTORSIZE);

	r = w25fs_deletedatablock(buf[inodeid].blocks, &sb);
	if (w25fs_iserror(r))
		return r;

	buf[inodeid].nextfree = sb.freeinodes;
	buf[inodeid].type = W25FS_EMPTY;
	buf[inodeid].size = 0;
	buf[inodeid].blocks = 0;

	sb.freeinodes = n;

	w25_erasesector(inodesector);
	w25_writesector(inodesector, (uint8_t *) buf);

	w25fs_writesuperblock(&sb);

	return 0;
}

uint32_t w25fs_inodeset(uint32_t n, uint8_t *data, uint32_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint8_t sectorbuf[W25_SECTORSIZE];
	size_t p;
	uint32_t block;

	w25fs_readsuperblock(&sb);
	w25fs_readinode(&in, n);

	if (n < sb.inodestart || n % sb.inodesz)
		return W25FS_WRONGADDR;

	if (sz > in.size)
		return W25FS_WRONGSIZE;

	block = in.blocks;
	for (p = 0; p < sz; p += W25_SECTORSIZE - sizeof(uint32_t)) {
		uint32_t next;
		uint32_t wsz;

		w25_read(block, (uint8_t *) (&next), sizeof(uint32_t));

		memmove(sectorbuf, (uint8_t *) (&next),
			sizeof(uint32_t));

		wsz = W25_SECTORSIZE - sizeof(uint32_t);
		wsz = (sz - p < wsz) ? (sz - p) : sz;

		memmove(sectorbuf + sizeof(uint32_t), data + p, wsz);
	
		w25_erasesector(block);
		w25_writesector(block, sectorbuf);

		block = next;
	}

	return 0;
}

uint32_t w25fs_inodeget(uint32_t n, uint8_t *data, uint32_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	size_t p;
	uint32_t block;
	uint32_t len;

	w25fs_readsuperblock(&sb);
	w25fs_readinode(&in, n);

	if (n < sb.inodestart || n % sb.inodesz)
		return W25FS_WRONGADDR;

	if (sz < in.size)
		return W25FS_WRONGSIZE;

	len = in.size;

	block = in.blocks;
	for (p = 0; p < len; p += W25_SECTORSIZE - sizeof(uint32_t)) {
		uint32_t next;
		uint32_t rlen;

		w25_read(block, (uint8_t *) (&next), sizeof(uint32_t));

		rlen = W25_SECTORSIZE - sizeof(uint32_t);
		rlen = (len - p < rlen) ? (len - p) : len;

		w25_read(block + sizeof(uint32_t), data + p, rlen);

		block = next;
	}

	return len;
}

int w25fs_splitpath(const char *path, char **toks, size_t sz)
{
	int i;
	static char pathbuf[W25FS_DIRRECORDSIZE];

	memmove(pathbuf, path, strlen(path));
	pathbuf[strlen(path)] = '\0';

	i = 0;

	toks[i++] = strtok(pathbuf, "/");

	while (i < sz && (toks[i++] = strtok(NULL, "/")) != NULL);

	--i;

	if (i >= sz)
		return (-1);

	if (toks[0] == NULL)
		return 0;

	return i;
}

static uint32_t w25fs_dirfindinode(uint8_t *buf, uint32_t n)
{
	uint32_t offset;

	for (offset = 0; ; offset += W25FS_DIRRECORDSIZE) {
		uint32_t nn;
		
		nn = 0xffffffff;

		memmove(&nn, buf + offset, sizeof(uint32_t));
		
		if (nn == n)
			return offset;
	}

	return 0xffffffff;
}

static uint32_t w25fs_dirsearch(uint8_t *buf, const char *name)
{
	uint32_t offset;

	for (offset = 0; ; offset += W25FS_DIRRECORDSIZE) {
		uint32_t n;

		n = 0xffffffff;

		memmove(&n, buf + offset, sizeof(uint32_t));
		
		if (n == 0xffffffff)
			return 0xffffffff;

		if (strcmp((char *) buf + offset
				+ sizeof(uint32_t), name) == 0) {
			return n;
		}
	}

	return 0xffffffff;
}

uint32_t w25fs_dirgetinode(const char **path)
{
	uint8_t buf[W25_SECTORSIZE];
	uint32_t parn;
	const char **p;

	parn = W25FS_ROOTINODE;
	for (p = path; *p != NULL; ++p) {
		struct w25fs_inode in;
		
		w25_read(parn, (uint8_t *) (&in),
			sizeof(struct w25fs_inode));

		if (in.type != W25FS_DIR)
			return 0xffffffff;

		w25fs_inodeget(parn, buf, W25_SECTORSIZE);

		if ((parn = w25fs_dirsearch(buf, *p)) == 0xffffffff)
			return 0xffffffff;
	}

	return parn;
}

static int w25fs_diradd(uint32_t parn, const char *name, uint32_t n)
{
	uint8_t buf[W25_SECTORSIZE];
	struct w25fs_inode in;
	uint32_t offset;
	uint32_t r;
	uint32_t b;

	w25fs_readinode(&in, parn);

	if (in.type != W25FS_DIR)
		return (-1);

	r = w25fs_inodeget(parn, buf,
		W25_SECTORSIZE - sizeof(uint32_t));
	if (w25fs_iserror(r))
		return (-1);

	offset = w25fs_dirfindinode(buf, 0xffffffff);
	
	memmove(buf + offset, (uint8_t *) &n, sizeof(uint32_t));
	strcpy((char *) buf + offset + sizeof(uint32_t), name);
	
	b = 0xffffffff;
	memmove(buf + offset + W25FS_DIRRECORDSIZE,
		(uint8_t *) &b, sizeof(uint32_t));

	w25fs_inodeset(parn, buf, W25_SECTORSIZE - sizeof(uint32_t));

	return 0;
}

static int _w25fs_dirdelete(uint32_t parn, const char *name, uint32_t n)
{
	uint8_t buf[W25_SECTORSIZE];
	struct w25fs_inode in;
	uint32_t offset, last;
	uint32_t r;
	uint32_t b;

	w25fs_readinode(&in, parn);

	if (in.type != W25FS_DIR)
		return (-1);

	r = w25fs_inodeget(parn, buf,
		W25_SECTORSIZE - sizeof(uint32_t));
	if (w25fs_iserror(r))
		return (-1);

	last = w25fs_dirfindinode(buf, 0xffffffff);

	if (last == 0)
		return (-1);

	if (last != W25FS_DIRRECORDSIZE)
		last -= W25FS_DIRRECORDSIZE;

	offset = w25fs_dirfindinode(buf, n);
	
	memmove(buf + offset, buf + last, W25FS_DIRRECORDSIZE);
	
	b = 0xffffffff;
	memmove(buf + last, (uint8_t *) &b, sizeof(uint32_t));

	w25fs_inodeset(parn, buf, W25_SECTORSIZE - sizeof(uint32_t));

	return 0;
}

int w25fs_dircreate(const char *path)
{
	char *toks[16];
	char *fname;
	uint32_t parn, n;
	uint32_t b;
	int tokc;

	tokc = w25fs_splitpath(path, toks, 16);

	n = w25fs_inodecreate(W25_SECTORSIZE - sizeof(uint32_t),
		W25FS_DIR);
	if (w25fs_iserror(n))
		return (-1);

	b = 0xffffffff;
	w25fs_inodeset(n, (uint8_t *) &b, sizeof(uint32_t));

	if (tokc == 0)
		return 0;

	fname = toks[tokc - 1];
	toks[tokc - 1] = NULL;

	if ((parn = w25fs_dirgetinode((const char **) toks)) < 0)
		return parn;

	return w25fs_diradd(parn, fname, n);
}

int w25fs_dirdelete(const char *path)
{
	char *toks[16];
	char *fname;
	uint32_t parn, n;
	int tokc;

	tokc = w25fs_splitpath(path, toks, 16);

	if ((n = w25fs_dirgetinode((const char **) toks)) < 0)
		return (-1);

	fname = toks[tokc - 1];
	toks[tokc - 1] = NULL;

	if (w25fs_iserror(parn = w25fs_dirgetinode((const char **) toks)))
		return (-1);
	
	if (_w25fs_dirdelete(parn, fname, n) < 0)
		return (-1);

	if (w25fs_iserror(w25fs_inodedelete(n)))
		return (-1);

	return 0;
}

int w25fs_dirlist(const char *path, char *lbuf, size_t sz)
{
	uint8_t buf[W25_SECTORSIZE];
	struct w25fs_inode in;
	char *toks[16];
	uint32_t offset;
	uint32_t n;

	w25fs_splitpath(path, toks, 16);

	if ((n = w25fs_dirgetinode((const char **) toks)) < 0)
		return n;

	w25fs_readinode(&in, n);

	if (in.type != W25FS_DIR)
		return (-1);

	w25fs_inodeget(n, buf, W25_SECTORSIZE);

	lbuf[0] = '\0';
	for (offset = 0; ; offset += W25FS_DIRRECORDSIZE) {
		uint32_t nn;

		nn = 0xffffffff;
	
		memmove(&nn, buf + offset, sizeof(uint32_t));

		if (nn == 0xffffffff)
			break;

		strcat(lbuf, (char *) buf + offset + sizeof(uint32_t));
		strcat(lbuf, "\n\r");
	}

	return 0;
}

int w25fs_filewrite(const char *path, const char *buf, size_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	char *toks[16];
	uint32_t n;

	w25fs_splitpath(path, toks, 16);

	if ((n = w25fs_dirgetinode((const char **) toks)) < 0)
		return (-1);

	w25fs_readsuperblock(&sb);
	w25fs_readinode(&in, n);

	in.type = W25FS_FILE;
	in.size = sz;
	
	if (w25fs_iserror(w25fs_deletedatablock(in.blocks, &sb)))
		return (-1);

	in.blocks = w25fs_createdatablock(&sb, sz);
	if (w25fs_iserror(in.blocks))
		return (-1);

	w25fs_writeinode(&in, n);

	if (w25fs_iserror(w25fs_inodeset(n, (uint8_t *) buf, sz)))
		return (-1);
	
	return 0;
}

int w25fs_fileread(const char *path, char *buf, size_t sz)
{
	char *toks[16];
	uint32_t n;

	w25fs_splitpath(path, toks, 16);

	if ((n = w25fs_dirgetinode((const char **) toks)) < 0)
		return (-1);

	if (w25fs_inodeget(n, (uint8_t *) buf, sz) < 0)
		return (-1);

	return 0;
}

int w25fs_dirstat(const char *path, struct w25fs_dirstat *st)
{
	struct w25fs_inode in;
	char *toks[16];
	uint32_t n;

	w25fs_splitpath(path, toks, 16);

	if ((n = w25fs_dirgetinode((const char **) toks)) < 0)
		return (-1);

	w25fs_readinode(&in, n);

	st->size = in.size;
	st->type = in.type;

	return 0;
}

char *w25fs_filetype(enum W25FS_INODETYPE type)
{
	char *W25FS_FILETYPE[] = {
		"empty", "file",
		"device", "directory"
	};

	return W25FS_FILETYPE[type];
}

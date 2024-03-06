#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <string.h>

#include "w25.h"

#define w25fs_checksum_t uint32_t

#define W25FS_INODESECTORSCOUNT 15
#define W25FS_INODESSZ (W25_SECTORSIZE * W25FS_INODESECTORSCOUNT)
#define W25FS_INODEPERSECTOR (W25_SECTORSIZE / sizeof(struct w25fs_inode))
#define W25FS_RETRYCOUNT 5
#define W25FS_DIRSIZE (W25_SECTORSIZE - sizeof(struct w25fs_blockmeta))
#define W25FS_DIRRECORDSIZE 32
#define W25FS_ROOTINODE 0x0001000

#define _W25FS_NODATABLOCKS	0xffffff01
#define _W25FS_WRONGADDR	0xffffff02
#define _W25FS_BADDATABLOCK	0xffffff03
#define _W25FS_WRONGSIZE	0xffffff04
#define _W25FS_PATHTOOLONG	0xffffff05
#define _W25FS_INODENOTFOUND	0xffffff06
#define _W25FS_NAMENOTFOUND	0xffffff07
#define _W25FS_NOTADIR		0xffffff08
#define _W25FS_NOTAFILE		0xffffff09
#define _W25FS_NOTAFILE		0xffffff09
#define _W25FS_EDIRNOTEMPTY	0xffffff0a
#define _W25FS_EALREADYEXISTS	0xffffff0b

#define w25fs_blockgetmeta(b) (((struct w25fs_blockmeta *) (b)))
#define w25fs_uint2interr(v) (-((v) & 0xff))
#define w25fs_iserror(v) ((v) > 0xffffff00)
#define w25fs_checksumembed(buf, size) \
	w25fs_checksum((uint8_t *) (buf) + sizeof(w25fs_checksum_t), \
			(size) - sizeof(w25fs_checksum_t))
#define w25fs_checkdataembed(dev, addr, size, cs) \
	w25fs_checkdata(dev, (addr) + sizeof(w25fs_checksum_t), \
			(size) - sizeof(w25fs_checksum_t), cs)
#define min(a, b) ((a) < (b) ? (a) : (b))

struct w25fs_superblock {
	w25fs_checksum_t checksum;
	uint32_t inodecnt;
	uint32_t inodesz;
	uint32_t inodestart;
	uint32_t freeinodes;
	uint32_t blockstart;
	uint32_t freeblocks;
	uint32_t inodechecksum[W25FS_INODESECTORSCOUNT + 1];
} __attribute__((packed));

struct w25fs_inode {
	uint32_t checksum;
	uint32_t nextfree;
	uint32_t blocks;
	uint32_t size;
	uint32_t type;
	uint32_t dummy0;
	uint32_t dummy1;
	uint32_t dummy2;
} __attribute__((packed));

struct w25fs_blockmeta {
	w25fs_checksum_t checksum;
	uint32_t next;
	uint32_t datasize;
} __attribute__((packed));

const int Delay[] = {0, 10, 100, 1000, 5000};

static int w25_getid(struct w25fs_device *dev)
{
	uint8_t sbuf[4], rbuf[4];

	sbuf[0] = 0x9f;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_SPI_Receive(dev->hspi, rbuf, 3, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return ((rbuf[0] << 16) | (rbuf[1] << 8) | rbuf[2]);
}

int w25_init(struct w25fs_device *dev)
{
	uint8_t sbuf[4];

	HAL_Delay(100);

	sbuf[0] = 0x66;
	sbuf[1] = 0x99;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 2, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	HAL_Delay(100);

	return w25_getid(dev);
}

static int w25_writeenable(struct w25fs_device *dev)
{
	uint8_t sbuf[4];

	sbuf[0] = 0x06;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return 0;
}

static int w25_writedisable(struct w25fs_device *dev)
{
	uint8_t sbuf[4];

	sbuf[0] = 0x04;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return 0;
}

static int w25_waitwrite(struct w25fs_device *dev)
{
	uint8_t sbuf[4], rbuf[4];

	sbuf[0] = 0x05;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);

	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);

	do {
		HAL_SPI_Receive(dev->hspi, rbuf, 1, 5000);
	} while ((rbuf[0] & 0x01) == 0x01);

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return 0;
}

static int w25_blockprotect(struct w25fs_device *dev, uint8_t flags)
{
	uint8_t sbuf[4];

	sbuf[0] = 0x50;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	sbuf[0] = 0x01;
	sbuf[1] = (flags & 0x0f) << 2;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 2, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return 0;
}

int w25fs_initdevice(struct w25fs_device *dev, SPI_HandleTypeDef *hspi,
	GPIO_TypeDef *gpio, uint16_t pin)
{
	dev->hspi = hspi;
	dev->gpio = gpio;
	dev->pin = pin;
	
	w25_init(dev);

	return 0;
}

int w25_read(struct w25fs_device *dev, uint32_t addr, uint8_t *data,
	uint32_t sz)
{
	uint8_t sbuf[4];

	sbuf[0] = 0x03;
	sbuf[1] = (addr >> 16) & 0xff;
	sbuf[2] = (addr >> 8) & 0xff;
	sbuf[3] = addr & 0xff;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 4, 5000);
	HAL_SPI_Receive(dev->hspi, data, sz, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return 0;
}

int w25_write(struct w25fs_device *dev, uint32_t addr,
	const uint8_t *data, uint32_t sz)
{
	uint8_t sbuf[4];

	w25_waitwrite(dev);

	w25_blockprotect(dev, 0x00);
	w25_writeenable(dev);

	sbuf[0] = 0x02;
	sbuf[1] = (addr >> 16) & 0xff;
	sbuf[2] = (addr >> 8) & 0xff;
	sbuf[3] = addr & 0xff;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 4, 5000);
	HAL_SPI_Transmit(dev->hspi, (uint8_t  *) data, sz, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	w25_waitwrite(dev);
	w25_writedisable(dev);
	w25_blockprotect(dev, 0x0f);

	return 0;
}

int w25_eraseall(struct w25fs_device *dev)
{
	uint8_t sbuf[4];

	w25_waitwrite(dev);
	w25_blockprotect(dev, 0x00);
	w25_writeenable(dev);

	sbuf[0] = 0xc7;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	w25_waitwrite(dev);
	w25_writedisable(dev);
	w25_blockprotect(dev, 0x0f);

	return 0;
}

int w25_erasesector(struct w25fs_device *dev, uint32_t addr)
{
	uint8_t sbuf[4];

	w25_waitwrite(dev);
	w25_blockprotect(dev, 0x00);
	w25_writeenable(dev);

	sbuf[0] = 0x20;
	sbuf[1] = (addr >> 16) & 0xff;
	sbuf[2] = (addr >> 8) & 0xff;
	sbuf[3] = addr & 0xff;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 4, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	w25_waitwrite(dev);
	w25_writedisable(dev);
	w25_blockprotect(dev, 0x0f);

	return 0;
}

int w25_eraseblock(struct w25fs_device *dev, uint32_t n)
{
	uint8_t sbuf[4];
	uint32_t addr;

	w25_waitwrite(dev);
	w25_blockprotect(dev, 0x00);
	w25_writeenable(dev);

	addr = n * W25_BLOCKSIZE;

	sbuf[0] = 0xD8;
	sbuf[1] = (addr >> 16) & 0xff;
	sbuf[2] = (addr >> 8) & 0xff;
	sbuf[3] = addr & 0xff;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 4, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	w25_waitwrite(dev);
	w25_writedisable(dev);
	w25_blockprotect(dev, 0x0f);

	return 0;
}

static int w25_writesector(struct w25fs_device *dev, uint32_t addr,
	const uint8_t *data, uint32_t sz)
{
	int i;

	for (i = 0; i < sz; i += min(W25_PAGESIZE, sz - i))
		w25_write(dev, addr + i, data + i, W25_PAGESIZE);

	return 0;
}

static int w25_rewritesector(struct w25fs_device *dev, uint32_t addr,
	const uint8_t *data, uint32_t sz)
{
	int i;

	w25_erasesector(dev, addr);

	for (i = 0; i < sz; i += min(W25_PAGESIZE, sz - i))
		w25_write(dev, addr + i, data + i, W25_PAGESIZE);

	return 0;
}

uint32_t w25fs_checksum(const void *buf, uint32_t size)
{
	uint32_t chk, i;

	chk = 0;
	for (i = 0; i < size / 4; ++i)
		chk ^= ((uint32_t *) buf)[i];

	return chk;
}

static int w25fs_checkdata(struct w25fs_device *dev, uint32_t addr,
	uint32_t size, w25fs_checksum_t cs)
{
	uint8_t buf[W25_PAGESIZE];
	uint32_t i, ccs;

	ccs = 0;
	for (i = 0; i < size; i += W25_PAGESIZE) {
		uint32_t cursz;

		cursz = min(size - i, W25_PAGESIZE);

		w25_read(dev, addr + i, buf, cursz);

		ccs ^= w25fs_checksum(buf, cursz);
	}

	return (ccs == cs);
}

static int w25fs_readsuperblock(struct w25fs_device *dev,
	struct w25fs_superblock *sb)
{
	uint32_t sz;
	int i;

	sz = sizeof(struct w25fs_superblock);

	for (i = 0; i < W25FS_RETRYCOUNT; ++i) {
		w25_read(dev, 0, (uint8_t *) (sb), sz);

		if (sb->checksum == w25fs_checksumembed(sb, sz))
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static int w25fs_writesuperblock(struct w25fs_device *dev,
	struct w25fs_superblock *sb)
{
	uint32_t sz;
	int i;

	sz = sizeof(struct w25fs_superblock);

	sb->checksum = w25fs_checksumembed(sb, sz);

	for (i = 0; i < W25FS_RETRYCOUNT; ++i) {
		struct w25fs_superblock sbb;

		w25_rewritesector(dev, 0, (uint8_t *) sb, sz);

		w25_read(dev, 0, (uint8_t *) &sbb, sizeof(sbb));

		if (sbb.checksum == w25fs_checksumembed(&sbb, sz))
			break;
		
		HAL_Delay(Delay[i]);
	}

	return 0;
}

static uint32_t w25fs_readinode(struct w25fs_device *dev,
	struct w25fs_inode *in, uint32_t n,
	const struct w25fs_superblock *sb)
{
	int i;
	uint32_t sz;

	sz = sizeof(struct w25fs_inode);

	for (i = 0; i < W25FS_RETRYCOUNT; ++i) {
		w25_read(dev, n, (uint8_t *) in, sz);

		if (in->checksum == w25fs_checksumembed(in, sz))
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static uint32_t w25fs_writeinode(struct w25fs_device *dev,
	const struct w25fs_inode *in, uint32_t n,
	struct w25fs_superblock *sb)
{
	struct w25fs_inode buf[W25FS_INODEPERSECTOR];
	uint32_t inodesector, inodesectorn, inodeid, sz;
	int i;

	sz = sizeof(struct w25fs_inode);

	inodesector = n / W25_SECTORSIZE * W25_SECTORSIZE;
	inodesectorn = inodesector / W25_SECTORSIZE;
	inodeid	= (n - inodesector) / sz;

	w25_read(dev, inodesector, (uint8_t *) buf, W25_SECTORSIZE);

	memmove(buf + inodeid, in, sz);

	buf[inodeid].checksum = w25fs_checksumembed(buf + inodeid, sz);

	sb->inodechecksum[inodesectorn]
		= w25fs_checksum(buf, W25_SECTORSIZE);

	for (i = 0; i < W25FS_RETRYCOUNT; ++i) {
		w25_rewritesector(dev, inodesector,
			(uint8_t *) buf, W25_SECTORSIZE);

		if (w25fs_checkdata(dev, inodesector, W25_SECTORSIZE,
			sb->inodechecksum[inodesectorn])) {
			break;
		}

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static uint32_t w25fs_readdatablock(struct w25fs_device *dev,
	uint32_t block, uint8_t *data)
{
	uint32_t totalsize;
	int i;

	for (i = 0; i < W25FS_RETRYCOUNT; ++i) {
		w25fs_checksum_t cs;

		w25_read(dev, block, data, W25_SECTORSIZE);

		totalsize = sizeof(struct w25fs_blockmeta)
			+ w25fs_blockgetmeta(data)->datasize;

		cs = w25fs_checksumembed(data, totalsize);

		if (w25fs_blockgetmeta(data)->checksum == cs)
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static uint32_t w25fs_writedatablock(struct w25fs_device *dev,
	uint32_t block, uint8_t *data)
{
	struct w25fs_blockmeta *meta;
	uint32_t totalsize;
	int i;

	meta = w25fs_blockgetmeta(data);

	totalsize = sizeof(struct w25fs_blockmeta) + meta->datasize;

	meta->checksum = w25fs_checksumembed(data, totalsize);

	for (i = 0; i < W25FS_RETRYCOUNT; ++i) {
		w25fs_checksum_t cs;

		w25_rewritesector(dev, block, data, totalsize);

		w25_read(dev, block, (uint8_t *) &cs,
			sizeof(w25fs_checksum_t));

		if (w25fs_checkdataembed(dev, block, totalsize, cs))
			break;

		HAL_Delay(Delay[i]);
	}

	return 0;
}

static uint32_t w25fs_createdatablock(struct w25fs_device *dev,
	struct w25fs_superblock *sb, uint32_t sz)
{
	uint32_t block, cursector, nextsector, restsz;
	struct w25fs_blockmeta meta;

	block = sb->freeblocks;

	restsz = sz + sz / W25_SECTORSIZE * sizeof(meta);
	cursector = nextsector = sb->freeblocks;
	while (restsz > 0 && nextsector != 0) {
		uint8_t buf[W25_SECTORSIZE];

		cursector = nextsector;

		restsz = (W25_SECTORSIZE > restsz)
			? 0 : restsz - W25_SECTORSIZE;

		w25fs_readdatablock(dev, cursector, buf);

		nextsector = w25fs_blockgetmeta(buf)->next;
	}

	if (nextsector == 0)
		return _W25FS_NODATABLOCKS;

	sb->freeblocks = nextsector;

	meta.next = 0;
	meta.datasize = 0;

	w25fs_writedatablock(dev, cursector, (uint8_t *) (&meta));

	return block;
}

static uint32_t w25fs_deletedatablock(struct w25fs_device *dev,
	uint32_t block, struct w25fs_superblock *sb)
{
	uint32_t cursector, nextsector;
	struct w25fs_blockmeta meta;

	if (block % W25_SECTORSIZE || block < sb->blockstart)
		return _W25FS_WRONGADDR;

	cursector = nextsector = block;
	while (nextsector != 0) {
		uint8_t buf[W25_SECTORSIZE];

		cursector = nextsector;

		w25fs_readdatablock(dev, cursector, buf);

		nextsector = w25fs_blockgetmeta(buf)->next;
	}

	if (cursector == 0)
		return _W25FS_BADDATABLOCK;

	meta.next = sb->freeblocks;
	meta.datasize = 0;

	w25fs_writedatablock(dev, cursector, (uint8_t *) (&meta));

	sb->freeblocks = block;

	return 0;
}

static uint32_t w25fs_inoderesize(struct w25fs_device *dev, uint32_t n,
	uint32_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t r;

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	if (sz <= in.size)
		return 0;

	in.size = sz;

	r = w25fs_deletedatablock(dev, in.blocks, &sb);
	if (w25fs_iserror(r))
		return r;

	in.blocks = w25fs_createdatablock(dev, &sb, sz);
	if (w25fs_iserror(in.blocks))
		return in.blocks;

	w25fs_writeinode(dev, &in, n, &sb);
	w25fs_writesuperblock(dev, &sb);

	return 0;
}

static int w25fs_writeinodeblock(struct w25fs_device *dev,
	uint32_t inodesectorn, uint8_t *buf,
	struct w25fs_superblock *sb)
{
	uint32_t inodesector;
	int i;

	inodesector = sb->inodestart + inodesectorn * W25_SECTORSIZE;

	sb->inodechecksum[1 + inodesectorn]
		= w25fs_checksum(buf, W25_SECTORSIZE);

	w25_writesector(dev, inodesector, buf, W25_SECTORSIZE);

	for (i = 0; i < W25FS_RETRYCOUNT; ++i) {
		if (w25fs_checkdata(dev, inodesector, W25_SECTORSIZE,
			sb->inodechecksum[1 + inodesectorn])) {
			break;
		}

		w25_rewritesector(dev, inodesector, buf, W25_SECTORSIZE);

		HAL_Delay(Delay[i]);
	}

	return 0;
}

uint32_t w25fs_format(struct w25fs_device *dev)
{
	struct w25fs_superblock sb;
	struct w25fs_inode buf[W25FS_INODEPERSECTOR];
	uint32_t inodecnt, p, i;

	w25_eraseall(dev);
	
	sb.inodecnt = W25FS_INODESSZ / sizeof(struct w25fs_inode);
	sb.inodesz = sizeof(struct w25fs_inode);
	sb.inodestart = W25_SECTORSIZE;
	sb.blockstart = W25_BLOCKSIZE;
	sb.freeinodes = W25_SECTORSIZE;
	sb.freeblocks = W25_BLOCKSIZE;

	inodecnt = W25FS_INODEPERSECTOR * W25FS_INODESECTORSCOUNT;
	for (i = 0; i < inodecnt; ++i) {
		struct w25fs_inode *curin;
		uint32_t insz;

		insz = sizeof(struct w25fs_inode);

		curin = buf + i % W25FS_INODEPERSECTOR;

		curin->nextfree = sb.inodestart + (i + 1) * sb.inodesz;
		curin->blocks = 0;
		curin->size = 0;
		curin->type = W25FS_EMPTY;

		curin->checksum = w25fs_checksumembed(curin, insz);

		if ((i + 1) % W25FS_INODEPERSECTOR == 0) {
			w25fs_writeinodeblock(dev,
				i / W25FS_INODEPERSECTOR,
				(uint8_t *) buf, &sb);
		}
	}

	w25fs_writesuperblock(dev, &sb);

	for (p = W25_BLOCKSIZE; p < W25_TOTALSIZE; p += W25_SECTORSIZE) {
		struct w25fs_blockmeta meta;
		uint32_t sz;
		int j;

		sz = sizeof(meta);

		meta.next = (p + W25_SECTORSIZE >= W25_TOTALSIZE)
			? 0 : p + W25_SECTORSIZE;
		meta.datasize = 0;

		meta.checksum = w25fs_checksumembed(&meta, sz);

		w25_writesector(dev, p, (uint8_t *) &meta, sz);

		for (j = 0; j < W25FS_RETRYCOUNT; ++j) {
			w25fs_checksum_t cs;

			w25_read(dev, p, (uint8_t *) &cs,
				sizeof(w25fs_checksum_t));

			if (w25fs_checkdataembed(dev, p, sz, cs))
				break;

			w25_rewritesector(dev, p, (uint8_t *) &meta, sz);

			HAL_Delay(Delay[j]);
		}
	}

	return w25fs_dircreate(dev, "/");
}

uint32_t w25fs_inodecreate(struct w25fs_device *dev, uint32_t sz,
	enum W25FS_INODETYPE type)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t oldfree;

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, sb.freeinodes, &sb);

	oldfree = sb.freeinodes;
	sb.freeinodes = in.nextfree;

	in.nextfree = 0;
	in.type = type;
	in.size = sz;

	in.blocks = w25fs_createdatablock(dev, &sb, sz);
	if (w25fs_iserror(in.blocks))
		return in.blocks;

	w25fs_writeinode(dev, &in, oldfree, &sb);
	w25fs_writesuperblock(dev, &sb);

	return oldfree;
}

uint32_t w25fs_inodedelete(struct w25fs_device *dev, uint32_t n)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t r;

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	if (n < sb.inodestart || n % sb.inodesz)
		return _W25FS_WRONGADDR;

	r = w25fs_deletedatablock(dev, in.blocks, &sb);
	if (w25fs_iserror(r))
		return r;

	in.nextfree = sb.freeinodes;
	in.type = W25FS_EMPTY;
	in.size = 0;
	in.blocks = 0;

	sb.freeinodes = n;

	w25fs_writeinode(dev, &in, n, &sb);
	w25fs_writesuperblock(dev, &sb);

	return 0;
}

uint32_t w25fs_inodeset(struct w25fs_device *dev, uint32_t n,
	const uint8_t *data, uint32_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t block, step, r, bsz, p;

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	if (n < sb.inodestart || n % sb.inodesz)
		return _W25FS_WRONGADDR;

	if (w25fs_iserror(r = w25fs_inoderesize(dev, n, sz)))
		return r;

	bsz = sizeof(struct w25fs_blockmeta);

	step = W25_SECTORSIZE - bsz;
	block = in.blocks;
	for (p = 0; p < sz; p += step) {
		uint8_t sectorbuf[W25_SECTORSIZE];
		struct w25fs_blockmeta *meta;

		w25fs_readdatablock(dev, block, sectorbuf);

		meta = w25fs_blockgetmeta(sectorbuf);

		meta->datasize = min(sz - p, step);

		memmove(sectorbuf + bsz, data + p, meta->datasize);

		w25fs_writedatablock(dev, block, sectorbuf);

		block = meta->next;
	}

	return 0;
}

uint32_t w25fs_inodeget(struct w25fs_device *dev, uint32_t n,
	uint8_t *data, uint32_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t block, step, bsz, p;

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	if (n < sb.inodestart || n % sb.inodesz)
		return _W25FS_WRONGADDR;

	if (sz < in.size)
		return _W25FS_WRONGSIZE;

	bsz = sizeof(struct w25fs_blockmeta);

	step = W25_SECTORSIZE - bsz;
	block = in.blocks;
	for (p = 0; p < in.size; p += step) {
		uint8_t sectorbuf[W25_SECTORSIZE];

		w25fs_readdatablock(dev, block, sectorbuf);

		memmove(data + p, sectorbuf + bsz, min(in.size - p, step));

		block = w25fs_blockgetmeta(sectorbuf)->next;
	}

	return in.size;
}

uint32_t w25fs_splitpath(const char *path, const char **toks, size_t sz)
{
	static char pathbuf[W25FS_DIRRECORDSIZE];
	int i;

	memmove(pathbuf, path, strlen(path) + 1);

	i = 0;

	toks[i++] = strtok(pathbuf, "/");

	while (i < sz && (toks[i++] = strtok(NULL, "/")) != NULL);

	--i;

	if (i >= sz)
		return _W25FS_PATHTOOLONG;

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

	return _W25FS_INODENOTFOUND;
}

static uint32_t w25fs_dirsearch(uint8_t *buf, const char *name)
{
	uint32_t offset;
	uint32_t sz;

	sz = sizeof(uint32_t);

	for (offset = 0; ; offset += W25FS_DIRRECORDSIZE) {
		uint32_t n;

		n = 0xffffffff;

		memmove(&n, buf + offset, sz);

		if (n == 0xffffffff)
			return _W25FS_NAMENOTFOUND;

		if (strcmp((char *) buf + offset + sz, name) == 0)
			return n;
	}

	return _W25FS_NAMENOTFOUND;
}

uint32_t w25fs_dirgetinode(struct w25fs_device *dev, const char **path)
{
	struct w25fs_superblock sb;
	uint8_t buf[W25FS_DIRSIZE];
	uint32_t parn;
	const char **p;

	w25fs_readsuperblock(dev, &sb);

	parn = W25FS_ROOTINODE;

	for (p = path; *p != NULL; ++p) {
		struct w25fs_inode in;

		w25fs_readinode(dev, (&in), parn, &sb);

		if (in.type != W25FS_DIR)
			return _W25FS_NOTADIR;

		w25fs_inodeget(dev, parn, buf, W25FS_DIRSIZE);

		if (w25fs_iserror(parn = w25fs_dirsearch(buf, *p)))
			return _W25FS_NAMENOTFOUND;
	}

	return parn;
}

static int w25fs_diradd(struct w25fs_device *dev, uint32_t parn,
	const char *name, uint32_t n)
{
	uint8_t buf[W25FS_DIRSIZE];
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t offset, r, b;

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, parn, &sb);

	if (in.type != W25FS_DIR)
		return W25FS_ENOTADIR;

	r = w25fs_inodeget(dev, parn, buf, W25FS_DIRSIZE);
	if (w25fs_iserror(r))
		return w25fs_uint2interr(r);	

	offset = w25fs_dirfindinode(buf, 0xffffffff);

	memmove(buf + offset, (uint8_t *) &n, sizeof(uint32_t));
	strcpy((char *) buf + offset + sizeof(uint32_t), name);

	b = 0xffffffff;
	memmove(buf + offset + W25FS_DIRRECORDSIZE,
		(uint8_t *) &b, sizeof(uint32_t));

	if ((w25fs_iserror(parn = w25fs_inodeset(dev, parn, buf,
			W25FS_DIRSIZE)))) {
		return w25fs_uint2interr(r);
	}

	return 0;
}

static int w25fs_dirdeleteinode(struct w25fs_device *dev,
	uint32_t parn, uint32_t n)
{
	uint8_t buf[W25FS_DIRSIZE];
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t offset, last, r, b;

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, parn, &sb);

	if (in.type != W25FS_DIR)
		return W25FS_ENOTADIR;

	r = w25fs_inodeget(dev, parn, buf, W25FS_DIRSIZE);
	if (w25fs_iserror(r))
		return w25fs_uint2interr(r);

	last = w25fs_dirfindinode(buf, 0xffffffff);
	if (last == 0)
		return (-1);

	if (last != W25FS_DIRRECORDSIZE)
		last -= W25FS_DIRRECORDSIZE;

	offset = w25fs_dirfindinode(buf, n);

	memmove(buf + offset, buf + last, W25FS_DIRRECORDSIZE);

	b = 0xffffffff;
	memmove(buf + last, (uint8_t *) &b, sizeof(uint32_t));

	w25fs_inodeset(dev, parn, buf, W25FS_DIRSIZE);

	return 0;
}

static int w25fs_dirisempty(struct w25fs_device *dev, uint32_t n)
{
	uint8_t buf[W25FS_DIRSIZE];
	uint32_t r, nn;

	r = w25fs_inodeget(dev, n, buf, W25FS_DIRSIZE);
	if (w25fs_iserror(r))	
		return w25fs_uint2interr(r);

	memmove(&nn, buf, sizeof(uint32_t));

	return ((nn != 0xffffffff) ? W25FS_EDIRNOTEMPTY : 0);
}

static int _w25fs_dirlist(struct w25fs_device *dev, uint32_t n,
	char *lbuf, size_t sz)
{
	uint8_t buf[W25FS_DIRSIZE];
	uint32_t offset, r;

	r = w25fs_inodeget(dev, n, buf, W25FS_DIRSIZE);
	if (w25fs_iserror(r))	
		return w25fs_uint2interr(r);

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

int w25fs_dircreate(struct w25fs_device *dev, const char *path)
{
	const char *toks[W25FS_PATHMAX];
	const char *fname;
	uint32_t parn, n, b;
	int tokc;

	tokc = w25fs_splitpath(path, toks, W25FS_PATHMAX);

	if (tokc != 0 && w25fs_dirgetinode(dev, toks)
			!= _W25FS_NAMENOTFOUND) {
		return W25FS_EALREADYEXISTS;
	}

	n = w25fs_inodecreate(dev, W25FS_DIRSIZE, W25FS_DIR);
	if (w25fs_iserror(n))
		return w25fs_uint2interr(n);

	b = 0xffffffff;
	w25fs_inodeset(dev, n, (uint8_t *) &b, sizeof(uint32_t));

	if (tokc == 0)
		return 0;

	fname = toks[tokc - 1];
	toks[tokc - 1] = NULL;

	if (w25fs_iserror(parn = w25fs_dirgetinode(dev, toks)))
		return w25fs_uint2interr(parn);

	return w25fs_diradd(dev, parn, fname, n);
}

int w25fs_dirdelete(struct w25fs_device *dev, const char *path)
{
	const char *toks[W25FS_PATHMAX];
	uint32_t parn, n;
	int tokc, r;

	tokc = w25fs_splitpath(path, toks, W25FS_PATHMAX);

	if (w25fs_iserror(n = w25fs_dirgetinode(dev, toks)))
		return w25fs_uint2interr(n);

	if ((r = w25fs_dirisempty(dev, n)) < 0)
		return r;

	toks[tokc - 1] = NULL;

	if (w25fs_iserror(parn = w25fs_dirgetinode(dev, toks)))
		return w25fs_uint2interr(parn);

	if ((r = w25fs_dirdeleteinode(dev, parn, n)) < 0)
		return r;

	if (w25fs_iserror(n = w25fs_inodedelete(dev, n)))
		return w25fs_uint2interr(parn);

	return 0;
}

int w25fs_dirlist(struct w25fs_device *dev, const char *path,
	char *lbuf, size_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	const char *toks[W25FS_PATHMAX];
	uint32_t n;

	w25fs_splitpath(path, toks, W25FS_PATHMAX);

	if (w25fs_iserror(n = w25fs_dirgetinode(dev, toks)))
		return w25fs_uint2interr(n);

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	if (in.type != W25FS_DIR)
		return W25FS_ENOTADIR;

	return _w25fs_dirlist(dev, n, lbuf, sz);
}

int w25fs_filewrite(struct w25fs_device *dev, const char *path,
	const uint8_t *buf, uint32_t sz)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	const char *toks[W25FS_PATHMAX];
	uint32_t n, r;

	w25fs_splitpath(path, toks, W25FS_PATHMAX);

	if (w25fs_iserror(n = w25fs_dirgetinode(dev, toks)))
		return w25fs_uint2interr(n);

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	in.type = W25FS_FILE;
	in.size = sz;

	r = w25fs_deletedatablock(dev, in.blocks, &sb);
	if (w25fs_iserror(r))
		return w25fs_uint2interr(r);

	in.blocks = w25fs_createdatablock(dev, &sb, sz);
	if (w25fs_iserror(in.blocks))
		return w25fs_uint2interr(in.blocks);

	w25fs_writeinode(dev, &in, n, &sb);
	w25fs_writesuperblock(dev, &sb);

	if (w25fs_iserror(n = w25fs_inodeset(dev, n, buf, sz)))
		return w25fs_uint2interr(n);

	return 0;
}

int w25fs_fileread(struct w25fs_device *dev, const char *path,
	uint8_t *buf, uint32_t sz)
{
	const char *toks[W25FS_PATHMAX];
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	uint32_t n;

	w25fs_splitpath(path, toks, W25FS_PATHMAX);

	if (w25fs_iserror(n = w25fs_dirgetinode(dev, toks)))
		return w25fs_uint2interr(n);

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	if (in.type != W25FS_FILE)
		return W25FS_ENOTAFILE;

	if (w25fs_iserror(n = w25fs_inodeget(dev, n, buf, sz)))
		return w25fs_uint2interr(n);

	return 0;
}

int w25fs_dirstat(struct w25fs_device *dev, const char *path,
	struct w25fs_dirstat *st)
{
	struct w25fs_superblock sb;
	struct w25fs_inode in;
	const char *toks[W25FS_PATHMAX];
	uint32_t n;
	int r;

	if ((r = w25fs_splitpath(path, toks, W25FS_PATHMAX)) < 0)
		return r;

	if (w25fs_iserror(n = w25fs_dirgetinode(dev, toks)))
		return w25fs_uint2interr(n);

	w25fs_readsuperblock(dev, &sb);
	w25fs_readinode(dev, &in, n, &sb);

	st->size = in.size;
	st->type = in.type;

	return 0;
}

const char *w25fs_strfiletype(enum W25FS_INODETYPE type)
{
	char *W25FS_FILETYPE[] = {
		"empty", "file", "device", "directory"
	};

	return W25FS_FILETYPE[type];
}

const char *w25fs_strerror(enum W25FS_ERROR e)
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
		"directory already exists"
	};

	return strerror[-e];
}

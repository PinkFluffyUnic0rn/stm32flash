#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "w25.h"

static struct w25_device devs[W25_MAXDEVS];
size_t devcount = 0;

#define min(a, b) ((a) < (b) ? (a) : (b))

static int w25_getid(struct w25_device *dev)
{
	uint8_t sbuf[4], rbuf[4];

	sbuf[0] = 0x9f;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_SPI_Receive(dev->hspi, rbuf, 3, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return ((rbuf[0] << 16) | (rbuf[1] << 8) | rbuf[2]);
}

static int w25_init(struct w25_device *dev)
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

static int w25_writeenable(struct w25_device *dev)
{
	uint8_t sbuf[4];

	sbuf[0] = 0x06;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return 0;
}

static int w25_writedisable(struct w25_device *dev)
{
	uint8_t sbuf[4];

	sbuf[0] = 0x04;

	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_RESET);
	HAL_SPI_Transmit(dev->hspi, sbuf, 1, 5000);
	HAL_GPIO_WritePin(dev->gpio, dev->pin, GPIO_PIN_SET);

	return 0;
}

static int w25_waitwrite(struct w25_device *dev)
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

static int w25_blockprotect(struct w25_device *dev, uint8_t flags)
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

int w25_read(void *d, size_t addr, void *data, size_t sz)
{
	struct w25_device *dev;
	uint8_t sbuf[4];

	dev = (struct w25_device *) d;

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

int w25_write(void *d, size_t addr, const void *data, size_t sz)
{
	struct w25_device *dev;
	uint8_t sbuf[4];
	
	dev = (struct w25_device *) d;

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

int w25_eraseall(void *d)
{
	struct w25_device *dev;
	uint8_t sbuf[4];
	
	dev = (struct w25_device *) d;

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

int w25_erasesector(void *d, size_t addr)
{
	struct w25_device *dev;
	uint8_t sbuf[4];
	
	dev = (struct w25_device *) d;

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

int w25_writesector(void *d, size_t addr, const void *data,
	size_t sz)
{
	struct w25_device *dev;
	int i;
	
	dev = (struct w25_device *) d;

	for (i = 0; i < sz; i += min(W25_PAGESIZE, sz - i))
		w25_write(dev, addr + i, data + i, W25_PAGESIZE);

	return 0;
}

int initdevice(void *is, struct device *dev)
{
	memmove(devs + devcount, is, sizeof(struct w25_device));
	
	sprintf(dev->name, "%s%d", "flash", devcount);

	dev->priv = devs + devcount;

	dev->read = w25_read;
	dev->write = w25_write;
	dev->eraseall = w25_eraseall;
	dev->erasesector = w25_erasesector;
	dev->writesector = w25_writesector;

	dev->writesize = W25_PAGESIZE;
	dev->sectorsize = W25_SECTORSIZE;
	dev->totalsize = W25_TOTALSIZE;

	w25_init(devs + devcount);

	devcount++;

	return 0;
}

int w25_getdriver(struct driver *driver)
{
	driver->initdevice = initdevice;

	return 0;
}

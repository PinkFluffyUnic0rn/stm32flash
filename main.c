#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "main.h"
#include "driver.h"
#include "vfs.h"
#include "filesystem.h"
#include "sfs.h"
#include "w25.h"
#include "uartterm.h"

#define PRESCALER 72
#define TIMPERIOD 0xffff
#define TICKSPERSEC (72000000 / PRESCALER)

#define ITDUR 10

#define OUTPUTPINSA (GPIO_PIN_4)
#define OUTPUTPINSB (GPIO_PIN_3)

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

struct driver drivers[8];
struct device dev[8];
struct filesystem fs[8];

struct device *curdev;

void systemclock_config(void);
static void gpio_init(void);
static void spi1_init(void);
static void tim1_init(void);
static void tim2_init(void);
static void usart1_init(void);
static void flash_init(void);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) 
{
}

int printhelp()
{
	ut_write("\r\nraw driver commands:\n\r");

	ut_write("\t%-23s%-32s\n\r",
		"sd [dev]", "set current device to [dev]");

	ut_write("\t%-23s%-32s\n\r",
		"rd [addr]", "read data at address [addr]");

	ut_write("\t%-23s%-32s\n\r",
		"wd [addr] [str]","write string [str] into [addr]");

	ut_write("\r\nraw filesystem commands:\n\r");
	
	ut_write("\t%-23s%-32s\n\r",
		"f", "format choosen device");

	ut_write("\t%-23s%-32s\n\r",
		"i [struct] {[addr]}",
		"dump filesystem [struct] (sb, in, bm) at [addr]");

	ut_write("\t%-23s%-32s\n\r",
		"c [sz]","create inode for data size of [size]");

	ut_write("\t%-23s%-32s\n\r",
		"d [addr]","delete inode with address [addr]");

	ut_write("\t%-23s%-32s\n\r",
		"s [addr] [data]",
		"set data for inode with address [addr] to [data]");

	ut_write("\t%-23s%-32s\n\r",
		"g [addr]",
		"get data from inode with address [addr]");

	ut_write("\r\nvirtual filesystem commands:\n\r");
	
	ut_write("\t%-23s%-32s\n\r",
		"mount [dev] [target]",
		"mount [dev] to [target]");

	ut_write("\t%-23s%-32s\n\r",
		"format [target]",
		"format device mounted at [target]");

	ut_write("\t%-23s%-32s\n\r",
		"umount [dev] [target]",
		"unmount [target]");

	ut_write("\t%-23s%-32s\n\r",
		"mountlist",
		"get list of mounted devices");

	ut_write("\t%-23s%-32s\n\r",
		"open [path] [flags]",
		"open file with [path], if [flags] is 'c', create it");

	ut_write("\t%-23s%-32s\n\r",
		"read [fd]",
		"read opened file with descriptor [fd]");

	ut_write("\t%-23s%-32s\n\r",
		"write [fd] [data]",
		"write [data] into opened file with descriptor [fd]");

	ut_write("\t%-23s%-32s\n\r",
		"close [fd]",
		"close opened file with descriptor [fd]");

	ut_write("\t%-23s%-32s\n\r",
		"mkdir [path]",
		"create directory [path]");

	ut_write("\t%-23s%-32s\n\r",
		"rm [path]",
		"delete file or directory [path]");

	ut_write("\t%-23s%-32s\n\r",
		"ls [path]",
		"get list of file in directory [path]");

	ut_write("\t%-23s%-32s\n\r",
		"cd [path]",
		"change current working directory to [path]");
	
	ut_write("\n\r");

	return 0;
}

int setdevice(const char **toks)
{
	if (strcmp(toks[1], dev[0].name) == 0)
		curdev = dev + 0;
	else if (strcmp(toks[1], dev[1].name) == 0)
		curdev = dev + 1;
	else {
		ut_write("unknown device %s\n\r", toks[1]);
		
		return 0;
	}

	ut_write("device %s was set\n\r", toks[1]);
	
	return 0;
}

int devformat(const char **toks)
{
	size_t r;

	if (fs_iserror(r = fs[0].format(curdev))) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	
		return 0;
	}

	return 0;
}

int readdata(const char **toks)
{
	char rdata[256];
	size_t addr, r;

	sscanf(toks[1], "%x", &addr);

	memset(rdata, 0, 256);

	r = curdev->read(curdev->priv, addr, rdata, 256);
	if (fs_iserror(r)) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	
		return 0;
	}

	ut_dumppage(rdata, 256);

	return 0;
}

int writedata(const char **toks)
{
	size_t addr, r;

	sscanf(toks[1], "%x", &addr);

	if (fs_iserror(r = curdev->erasesector(curdev->priv, addr))) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	
		return 0;
	}

	r = curdev->write(curdev->priv, addr, toks[2],
		strlen(toks[2]) + 1);
	if (fs_iserror(r)) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));

		return 0;
	}

	return 0;
}

int mntdevformat(const char **toks)
{
	int r;
	
	if ((r = format(toks[1])) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));
	
		return 0;
	}

	return 0;
}

int createinode(const char **toks)
{
	size_t addr, r;

	sscanf(toks[1], "%d", &addr);

	if (fs_iserror(r = fs[0].inodecreate(curdev, addr, FS_FILE))) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
		
		return 0;
	}

	ut_write("new inode address: %x\n\r", r);

	return 0;
}

int deleteinode(const char **toks)
{
	size_t addr, r;

	sscanf(toks[1], "%x", &addr);

	if (fs_iserror(r = fs[0].inodedelete(curdev, addr))) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	
		return 0;
	}

	return 0;
}

int setinode(const char **toks)
{
	size_t addr, r;

	sscanf(toks[1], "%x", &addr);

	r = fs[0].inodeset(curdev, addr, toks[2], strlen(toks[2]) + 1);
	if (fs_iserror(r)) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	
		return 0;
	}

	return 0;
}

int getinode(const char **toks)
{
	size_t addr;
	uint8_t buf[256];
	size_t r;

	sscanf(toks[1], "%x", &addr);

	if (fs_iserror(r = fs[0].inodeget(curdev, addr, buf, 256))) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	
		return 0;
	}

	buf[r] = '\0';

	ut_write("%s\n\r", buf);

	return 0;
}

int readinode(const char **toks)
{
	size_t addr, offset, size;
	uint8_t buf[256];
	size_t r;

	sscanf(toks[1], "%x", &addr);
	sscanf(toks[2], "%d", &offset);
	sscanf(toks[3], "%d", &size);

	memset(buf, 0, 256);

	r = fs[0].inoderead(curdev, addr, offset, buf, size);
	if (fs_iserror(r)) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	
		return 0;
	}

	ut_write("%s\n\r", buf);

	return 0;
}

int writeinode(const char **toks)
{
	size_t addr, offset, r;

	sscanf(toks[1], "%x", &addr);
	sscanf(toks[2], "%d", &offset);

	r = fs[0].inodewrite(curdev, addr, offset,
		toks[3], strlen(toks[3]));
	if (fs_iserror(r)) {
		ut_write("error: %s\n\r",
			vfs_strerror(fs_uint2interr(r)));
	}

	return 0;
}

int createbiginode(const char **toks)
{
	size_t sz, addr, insz, i;
	char buf[5000];

	insz = 5000;

	sscanf(toks[1], "%d", &sz);

	ut_write("new inode address: %x\n\r",
		(addr = fs[0].inodecreate(curdev, 32, FS_FILE)));

	for (i = 0; i < insz; ++i)
		buf[i] = (i % ('z' - 'a')) + 'a';

	ut_write("set data: %d\n\r",
		fs[0].inodeset(curdev, addr, buf, insz));

	return 0;
}

int dumpsb()
{
	struct sfs_superblock sb;
	int i;

	fs[0].dumpsuperblock(curdev, &sb);

	ut_write("checksum: %lx\r\n", sb.checksum);
	ut_write("inode count: %lx\r\n", sb.inodecnt);
	ut_write("inode size: %lu\r\n", sb.inodesz);
	ut_write("inodes start: %lx\r\n", sb.inodestart);
	ut_write("free inode: %lx\r\n", sb.freeinodes);
	ut_write("blocks start: %lx\r\n", sb.blockstart);
	ut_write("free block: %lx\r\n", sb.freeblocks);

	ut_write("inodes checksums: ");
	for (i = 0; i < SFS_INODESECTORSCOUNT + 1; ++i) {
		ut_write("%lx%s", sb.inodechecksum[i],
			((i != SFS_INODESECTORSCOUNT) ? ", " : ""));
	}
	ut_write("\r\n");

	return 0;
}

int dumpin(const char *arg)
{
	size_t addr;
	struct sfs_inode in;

	sscanf(arg, "%x", &addr);

	fs[0].dumpinode(curdev, addr, &in);

	ut_write("checksum: %lx\r\n", in.checksum);
	ut_write("next free: %lx\r\n", in.nextfree);
	ut_write("size: %lu\r\n", in.size);
	ut_write("allocsize: %lu\r\n", in.allocsize);
	ut_write("type: %lx\r\n", in.type);
	ut_write("block[0]: %x\r\n", in.blocks.block[0]);
	ut_write("block[1]: %x\r\n", in.blocks.block[1]);
	ut_write("indirect block: %x\r\n", in.blocks.blockindirect);

	return 0;
}

int dumpb(const char *arg)
{
	size_t addr;
	struct sfs_blockmeta meta;

	sscanf(arg, "%x", &addr);

	fs[0].dumpblockmeta(curdev, addr, &meta);

	ut_write("checksum: %lx\r\n", meta.checksum);
	ut_write("next: %lx\r\n", meta.next);
	ut_write("datasize: %lu\r\n", meta.datasize);

	return 0;
}

int dump(const char **toks)
{
	if (strcmp(toks[1], "sb") == 0)
		return dumpsb();
	if (strcmp(toks[1], "in") == 0)
		return dumpin(toks[2]);
	if (strcmp(toks[1], "bm") == 0)
		return dumpb(toks[2]);
	else {
		ut_write("Unknown structure\n\r");

		return 0;
	}

	return 0;
}


int mounthandler(const char **toks)
{
	struct device *d;
	int r;

	if (strcmp(toks[1], dev[0].name) == 0)
		d = dev + 0;
	else if (strcmp(toks[1], dev[1].name) == 0)
		d = dev + 1;
	else {
		ut_write("unknown device %s\n\r", toks[1]);
	
		return 0;
	}

	if ((r = mount(d, toks[2], fs + 0)) < 0) {
		ut_write("mount: %s\n\r", vfs_strerror(r));

		return 0;
	}

	return 0;
}

int umounthandler(const char **toks)
{
	int r;

	if ((r = umount(toks[1])) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));
	
		return 0;
	}

	return 0;
}

int mountlisthandler(const char **toks)
{
	char buf[256];
	const char *list[MOUNTMAX];
	const char **p;
	int r;

	if ((r = mountlist(list, buf, 2048)) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));

		return 0;
	}

	for (p = list; *p != NULL; ++p)
		ut_write("%s\n\r", *p);
		
	return 0;
}

int openfile(const char **toks)
{
	int fd;

	fd = open(toks[1], (strcmp(toks[2], "c") == 0) ? O_CREAT : 0);

	if (fd < 0) {
		ut_write("error: %s\n\r", vfs_strerror(fd));
		
		return 0;
	}

	ut_write("fd: %d\n\r", fd);

	return 0;
}

int readfile(const char **toks)
{
	uint8_t buf[256];
	size_t sz;
	int fd, r;

	sscanf(toks[1], "%d", &fd);
	sscanf(toks[2], "%d", &sz);

	if ((r = read(fd, buf, sz)) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));
		
		return 0;
	}

	buf[r] = '\0';
	ut_write("%s\n\r", (char *) buf);

	return 0;
}

int writefile(const char **toks)
{
	int fd, r;
	
	sscanf(toks[1], "%d", &fd);

	if ((r = write(fd, toks[2], strlen(toks[2]))) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));
		
		return 0;
	}

	return 0;
}

int closefile(const char **toks)
{
	int fd, r;

	sscanf(toks[1], "%d", &fd);

	if ((r = close(fd)) < 0) {
		ut_write("error %d: %s\n\r", fd, vfs_strerror(r));
		
		return 0;
	}

	return 0;
}

int makedir(const char **toks)
{
	int r;

	if ((r = mkdir(toks[1])) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));

		return 0;
	}

	return 0;
}

int unlinkfile(const char **toks)
{
	int r;

	if ((r = unlink(toks[1])) < 0) {
		ut_write("unlink: %s\n\r", vfs_strerror(r));
	
		return 0;
	}

	return 0;
}

int listvfsdir(const char **toks)
{
	char buf[256];
	char *list[16];
	char **p;
	int r;

	if ((r = lsdir(toks[1], (const char **) list, buf, 256)) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));
		
		return 0;
	}

	for (p = list; *p != NULL; ++p)
		ut_write("%s\n\r", *p);

	return 0;
}

int vfscd(const char **toks)
{
	int r;

	if ((r = cd(toks[1])) < 0) {
		ut_write("error: %s\n\r", vfs_strerror(r));
	
		return 0;
	}

	return 0;
}

int main(void)
{
	HAL_Init();

	systemclock_config();

	gpio_init();
	tim1_init();
	tim2_init();
	usart1_init();
	spi1_init();
	flash_init();

	HAL_TIM_Base_Start_IT(&htim1);
	HAL_TIM_Base_Start_IT(&htim2);

	__HAL_TIM_SET_COUNTER(&htim2, 0);

	ut_init(&huart1);

	ut_addcommand("sd",		setdevice);
	ut_addcommand("rd",		readdata);
	ut_addcommand("wd",		writedata);

	ut_addcommand("f",		devformat);
	ut_addcommand("i",		dump);
	ut_addcommand("c",		createinode);
	ut_addcommand("d",		deleteinode);
	ut_addcommand("s",		setinode);
	ut_addcommand("g",		getinode);
	ut_addcommand("r",		readinode);
	ut_addcommand("w",		writeinode);
	ut_addcommand("b",		createbiginode);


	ut_addcommand("format",		mntdevformat);
	ut_addcommand("mount",		mounthandler);
	ut_addcommand("umount",		umounthandler);
	ut_addcommand("mountlist",	mountlisthandler);
	ut_addcommand("open",		openfile);
	ut_addcommand("read",		readfile);
	ut_addcommand("write",		writefile);
	ut_addcommand("close",		closefile);
	ut_addcommand("mkdir",		makedir);
	ut_addcommand("rm",		unlinkfile);
	ut_addcommand("ls",		listvfsdir);
	ut_addcommand("cd",		vfscd);

	printhelp();

	mount(dev + 0, "/", fs + 0);
	mount(dev + 1, "/dev", fs + 0);

	ut_promptcommand();

	while (1) {
		int c;

		__HAL_TIM_SET_COUNTER(&htim2, 0);

		if (ut_getcommand() == 0) {
			ut_executecommand();
			ut_promptcommand();
		}

		while ((c = __HAL_TIM_GET_COUNTER(&htim2)) < ITDUR);
	}
}

void systemclock_config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
		error_handler();

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK
		| RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1
		| RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct,
			FLASH_LATENCY_0) != HAL_OK)
		error_handler();
}

static void gpio_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	HAL_GPIO_WritePin(GPIOA, OUTPUTPINSA, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOB, OUTPUTPINSB, GPIO_PIN_RESET);

	GPIO_InitStruct.Pin = OUTPUTPINSA;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = OUTPUTPINSB;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

static void spi1_init(void)
{
	hspi1.Instance = SPI1;
	hspi1.Init.Mode = SPI_MODE_MASTER;
	hspi1.Init.Direction = SPI_DIRECTION_2LINES;
	hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
	hspi1.Init.NSS = SPI_NSS_SOFT;
	hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
	hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
	hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
	hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	hspi1.Init.CRCPolynomial = 10;

	if (HAL_SPI_Init(&hspi1) != HAL_OK)
		error_handler();
}

static void tim1_init(void)
{
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};

	htim1.Instance = TIM1;
	htim1.Init.Prescaler = 72 - 1;
	htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim1.Init.Period = 0xffff - 1;
	htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

	if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
		error_handler();

	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;

	if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
		error_handler();

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

	if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
		error_handler();
}

static void tim2_init(void)
{
	TIM_ClockConfigTypeDef sClockSourceConfig = {0};
	TIM_MasterConfigTypeDef sMasterConfig = {0};

	htim2.Instance = TIM2;
	htim2.Init.Prescaler = PRESCALER - 1;
	htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim2.Init.Period = TIMPERIOD - 1;
	htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

	if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
		error_handler();

	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;

	if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
		error_handler();

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;

	if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
		error_handler();
}

static void usart1_init()
{
	huart1.Instance = USART1;
	huart1.Init.BaudRate = 921600;
	huart1.Init.WordLength = UART_WORDLENGTH_8B;
	huart1.Init.StopBits = UART_STOPBITS_1;
	huart1.Init.Parity = UART_PARITY_NONE;
	huart1.Init.Mode = UART_MODE_TX_RX;
	huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart1.Init.OverSampling = UART_OVERSAMPLING_16;

	if (HAL_UART_Init(&huart1) != HAL_OK)
		error_handler();
}

static void flash_init()
{
	struct w25_device d;

	w25_getdriver(drivers + 0);

	d.hspi = &hspi1;
	d.gpio = GPIOA;
	d.pin = GPIO_PIN_4;
	drivers[0].initdevice(&d, dev + 0);

	d.hspi = &hspi1;
	d.gpio = GPIOB;
	d.pin = GPIO_PIN_3;
	drivers[0].initdevice(&d, dev + 1);

	curdev = dev;

	sfs_getfs(fs + 0);
}

void error_handler(void)
{
	__disable_irq();
	while (1) {}
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{

}
#endif

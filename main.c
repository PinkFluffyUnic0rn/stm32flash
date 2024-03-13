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
	char s[4096];

	sprintf(s, "\r\ncommands:\n\r");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"r [addr]", "read data at address [addr]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"w [addr] [str]","write string [str] into [addr]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"f","format flash");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"c [sz]","create inode for data size of [size]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"d [addr]","delete inode with address [addr]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"s [addr] [data]",
		"set data for inode with address [addr] to [data]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"h [addr] [size]",
		"calculate checksum for data at [addr] of [size]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"g [addr]",
		"get data from inode with address [addr]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"create [path]",
		"create directory [path]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"delete [path]",
		"delete directory [path]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"set [path] [data]",
		"set [data] into directory [path], turning it into a file");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"get [path]",
		"get data from file [path]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"list [path]",
		"list all subdirectories of directory [path]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"stat [path]",
		"show all attributes of directory [path]");	

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"path [path]",
		"split [path] into tokens");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"getinode [path]",
		"get inode address for directory [path]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"device [dev]",
		"set current device to [dev]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"mount [dev] [target]",
		"mount [dev] to [target]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"umount [dev] [target]",
		"unmount [target]");

	sprintf(s + strlen(s), "\t%-23s%-32s\n\r",
		"mountlist",
		"get list of mounted devices");

	HAL_UART_Transmit(&huart1, (uint8_t *) s, strlen(s), 100);

	return 0;
}

int readdata(const char **toks)
{
	char b[2048];
	char rdata[256];
	size_t addr;

	sscanf(toks[1], "%x", &addr);

	memset(rdata, 0, 256);
	curdev->read(curdev->priv, addr, rdata, 256);

	ut_dumppage(rdata, b, 2048);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int writedata(const char **toks)
{
	size_t addr;

	sscanf(toks[1], "%x", &addr);

	curdev->erasesector(curdev->priv, addr);
	curdev->write(curdev->priv, addr, toks[2], strlen(toks[2]) + 1);

	return 0;
}

int format(const char **toks)
{
	fs[0].format(curdev);

	return 0;
}

int createinode(const char **toks)
{
	size_t addr;
	char b[1024];

	sscanf(toks[1], "%d", &addr);

	sprintf(b, "new inode address: %x\n\r",
		fs[0].inodecreate(curdev, addr, FS_FILE));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int deleteinode(const char **toks)
{
	size_t addr;
	char b[1024];

	sscanf(toks[1], "%x", &addr);

	sprintf(b, "deleted addr: %x\n\r",
		fs[0].inodedelete(curdev, addr));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int setinode(const char **toks)
{
	size_t addr;
	char b[1024];

	sscanf(toks[1], "%x", &addr);

	sprintf(b, "set data: %d\n\r",
		fs[0].inodeset(curdev, addr, toks[2], strlen(toks[2]) + 1));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int getinode(const char **toks)
{
	size_t addr;
	uint8_t buf[1024];
	char b[4096];
	size_t r;

	sscanf(toks[1], "%x", &addr);

	r = fs[0].inodeget(curdev, addr, buf, 1024);

	sprintf(b, "got data: %x |%s|\n\r", r, buf);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int createdir(const char **toks)
{
	char buf[1024];

	sprintf(buf, "creating directory: %s\n\r",
		vfs_strerror(fs[0].dircreate(curdev, toks[1])));

	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}

int deletedir(const char **toks)
{
	char buf[1024];

	sprintf(buf, "deleting directory: %s\n\r",
		vfs_strerror(fs[0].dirdelete(curdev, toks[1])));

	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}

int setfile(const char **toks)
{
	char buf[1024];

	sprintf(buf, "writing file: %s\n\r",
		vfs_strerror(fs[0].filewrite(curdev, toks[1],
			toks[2],
			strlen(toks[2]) + 1)));

	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}

int getfile(const char **toks)
{
	uint8_t buf[1024];
	char b[4096];
	enum ERROR r;

	r = fs[0].fileread(curdev, toks[1], buf, 1024);

	sprintf(b, "got data (%s): |%s|\n\r", vfs_strerror(r),
		(char *) buf);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int listdir(const char **toks)
{
	char buf[512];
	char b[1024];
	enum ERROR r;

	buf[0] = '\0';
	r = fs[0].dirlist(curdev, toks[1], buf, 512);

	sprintf(b, "directory list (%s):\n\r%s",
		vfs_strerror(r), (r == ESUCCESS ? buf : ""));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int statdir(const char **toks)
{
	struct fs_dirstat stat;
	char buf[1024];
	enum ERROR r;

	r = fs[0].dirstat(curdev, toks[1], &stat);

	sprintf(buf, "result: %s\n\rsize: %d\n\rtype: %s\n\r",
		vfs_strerror(r), stat.size,
		fs_strfiletype(stat.type));

	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}

int setdevice(const char **toks)
{
	if (strcmp(toks[1], "dev1") == 0)
		curdev = dev + 0;
	else if (strcmp(toks[1], "dev2") == 0)
		curdev = dev + 1;

	return 0;
}


int mounthandler(const char **toks)
{
	char b[4096];

	struct device *d;

	if (strcmp(toks[1], "dev1") == 0)
		d = dev + 0;
	else if (strcmp(toks[1], "dev2") == 0)
		d = dev + 1;
	else
		return 0;

	sprintf(b, "mount: %s\n\r",
		vfs_strerror(mount(d, toks[2], fs + 0)));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int umounthandler(const char **toks)
{
	char b[4096];

	sprintf(b, "umount: %s\n\r", vfs_strerror(umount(toks[1])));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int mountlisthandler(const char **toks)
{
	char b[4096];
	char buf[2048];
	char *list[MOUNTMAX];
	char **p;

	sprintf(b, "mount list: %s\n\r", vfs_strerror(mountlist(
		(const char **) list, buf, 2048)));

	for (p = list; *p != NULL; ++p)
		sprintf(b + strlen(b), "%s\n\r", *p);
		
	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int openfile(const char **toks)
{
	char b[4096];
	int fd;

	fd = open(toks[1], 0);

	if (fd < 0)
		sprintf(b, "open: %s\n\r", vfs_strerror(fd));
	else
		sprintf(b, "open: %d\n\r", fd);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int readfile(const char **toks)
{
	uint8_t buf[1024];
	char b[4096];
	int fd;
	
	sscanf(toks[1], "%d", &fd);

	sprintf(b, "reading %d: %s\n\r", fd,
		vfs_strerror(read(fd, buf, 1024)));

	sprintf(b + strlen(b), "got data: |%s|\n\r", (char *) buf);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int writefile(const char **toks)
{
	char b[4096];
	int fd;
	
	sscanf(toks[1], "%d", &fd);

	sprintf(b, "write %d: %s\n\r", fd,
		vfs_strerror(write(fd, toks[2], strlen(toks[2]) + 1)));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int closefile(const char **toks)
{
	char b[4096];
	int fd;

	sscanf(toks[1], "%d", &fd);

	sprintf(b, "closing %d: %s\n\r", fd, vfs_strerror(close(fd)));

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

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

	ut_addcommand("r",		readdata);
	ut_addcommand("w",		writedata);
	ut_addcommand("f",		format);
	ut_addcommand("c",		createinode);
	ut_addcommand("d",		deleteinode);
	ut_addcommand("s",		setinode);
	ut_addcommand("g",		getinode);

	ut_addcommand("device",		setdevice);
	ut_addcommand("create",		createdir);
	ut_addcommand("delete",		deletedir);
	ut_addcommand("set",		setfile);
	ut_addcommand("get",		getfile);
	ut_addcommand("list",		listdir);
	ut_addcommand("stat",		statdir);
	ut_addcommand("mount",		mounthandler);
	ut_addcommand("umount",		umounthandler);
	ut_addcommand("mountlist",	mountlisthandler);
	ut_addcommand("open",		openfile);
	ut_addcommand("read",		readfile);
	ut_addcommand("write",		writefile);
	ut_addcommand("close",		closefile);

	printhelp();

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

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
		error_handler();

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK
		| RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1
		| RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
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

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "main.h"
#include "w25.h"
#include "uartterm.h"

#define PRESCALER 72
#define TIMPERIOD 0xffff
#define TICKSPERSEC (72000000 / PRESCALER)

#define ITDUR 10

#define OUTPUTPINSA (GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_8 \
	| GPIO_PIN_11 | GPIO_PIN_12)
#define OUTPUTPINSB (GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 \
	| GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11 | \
	GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart1;

void systemclock_config(void);
static void gpio_init(void);
static void spi1_init(void);
static void tim1_init(void);
static void tim2_init(void);
static void usart1_init(void);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) 
{
}

int printhelp()
{
	char s[4096];

	sprintf(s, "\r\ncommands:\n\r");
	
	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"r [addr]", "read data at address [addr]");
	
	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"w [addr] [str]","write string [str] into [addr]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"f","format flash");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"c [sz]","create inode for data size of [size]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"d [addr]","delete inode with address [addr]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"s [addr] [data]",
		"set data for inode with address [addr] to [data]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"g [addr]",
		"get data from inode with address [addr]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"create [path]",
		"create directory [path]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"delete [path]",
		"delete directory [path]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"write [path] [data]",
		"write [data] into directory [path], turning it into a file");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"read [path]",
		"read data from file [path]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"list [path]",
		"list all subdirectories of directory [path]");

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"stat [path]",
		"show all attributes of directory [path]");	

	sprintf(s + strlen(s), "\t%-16s%-32s\n\r",
		"getinode [path]",
		"get inode address for directory [path]");

	HAL_UART_Transmit(&huart1, (uint8_t *) s, strlen(s), 100);

	return 0;
}

int readdata(const char **toks)
{
	char b[8192];
	uint8_t rdata[256];
	uint32_t addr;

	sscanf(toks[1], "%lx", &addr);

	memset(rdata, 0, 256);
	w25_read(addr, rdata, 256);

	ut_dumppage(rdata, b, 8192);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);

	return 0;
}

int writedata(const char **toks)
{
	uint32_t addr;

	sscanf(toks[1], "%lx", &addr);

	w25_erasesector(addr / 4096);
	w25_write(addr, (uint8_t *) toks[2], strlen(toks[2]) + 1);
	
	return 0;
}

int format(const char **toks)
{
	w25fs_format();

	return 0;
}

int createinode(const char **toks)
{
	uint32_t addr;
	char b[1024];
	
	sscanf(toks[1], "%ld", &addr);
	
	sprintf(b, "new inode address: %lx\n\r",
		w25fs_inodecreate(addr, W25FS_FILE));
	
	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);
	
	return 0;
}

int deleteinode(const char **toks)
{
	uint32_t addr;
	char b[1024];
	
	sscanf(toks[1], "%lx", &addr);
	
	sprintf(b, "deleted addr: %lx\n\r",
		w25fs_inodedelete(addr));
	
	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);
	
	return 0;
}

int setinode(const char **toks)
{
	uint32_t addr;

	sscanf(toks[1], "%lx", &addr);

	w25fs_inodeset(addr, (uint8_t *) toks[2], strlen(toks[2]) + 1);

	return 0;
}

int getinode(const char **toks)
{
	uint32_t addr;
	uint8_t buf[1024];
	char b[4096];
	uint32_t r;

	sscanf(toks[1], "%lx", &addr);

	r = w25fs_inodeget(addr, buf, 1024);
		
	sprintf(b, "got data: %lx |%s|\n\r", r, buf);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);
	
	return 0;
}

int createdir(const char **toks)
{
	char buf[1024];
	
	sprintf(buf, "creating directory: %d\n\r",
		w25fs_dircreate(toks[1]));
	
	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);
	
	return 0;
}

int deletedir(const char **toks)
{
	char buf[1024];
	
	sprintf(buf, "deleting directory: %d\n\r",
		w25fs_dirdelete(toks[1]));
	
	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);
	
	return 0;
}

int writefile(const char **toks)
{
	char buf[1024];
	
	sprintf(buf, "writing file: %d\n\r",
		w25fs_filewrite(toks[1], toks[2], strlen(toks[2]) + 1));
	
	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}


int readfile(const char **toks)
{
	char buf[1024];
	char b[4096];

	w25fs_fileread(toks[1], buf, 1024);
		
	sprintf(b, "got data: |%s|\n\r", buf);

	HAL_UART_Transmit(&huart1, (uint8_t *) b, strlen(b), 100);
	
	return 0;
}

int listdir(const char **toks)
{
	char buf[4096];

	buf[0] = '\0';
	w25fs_dirlist(toks[1], buf, 4096);

	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}

int splitpath(const char **toks)
{
	char buf[1024];
	char *parts[16];
	int partc;
	char **p;

	partc = w25fs_splitpath(toks[1], parts, 16);

	sprintf(buf, "afer split (partc: %d):\n\r", partc);

	for (p = parts; *p != NULL; ++p)
		sprintf(buf + strlen(buf), "|%s|\n\r", *p);
	
	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}

int dirgetinode(const char **toks)
{
	char buf[1024];
	char *parts[16];

	w25fs_splitpath(toks[1], parts, 16);
	
	sprintf(buf, "directory inode: %lx\n\r",
		w25fs_dirgetinode((const char **) parts));
	
	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);
	
	return 0;
}

int statdir(const char **toks)
{
	struct w25fs_dirstat stat;
	char buf[1024];

	w25fs_dirstat(toks[1], &stat);

	sprintf(buf, "size: %ld\n\rtype: %s\n\r",
		stat.size, w25fs_filetype(stat.type));
	
	HAL_UART_Transmit(&huart1, (uint8_t *) buf, strlen(buf), 100);

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
	w25_init(&hspi1);

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
	ut_addcommand("create",		createdir);
	ut_addcommand("delete",		deletedir);
	ut_addcommand("write",		writefile);
	ut_addcommand("read",		readfile);
	ut_addcommand("list",		listdir);
	ut_addcommand("path",		splitpath);
	ut_addcommand("getinode",	dirgetinode);
	ut_addcommand("stat",		statdir);

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

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

#include "uartterm.h"

#define RXBUFSZ 32
#define MAXTOKS 16

struct ut_command {
	const char *name;
	int (*func)(const char **);
};

static uint8_t Rxdata[32];
static size_t Rxoffset = 0;

static int Gotcommand;
static char *Toks[MAXTOKS];

static UART_HandleTypeDef *huart;

static struct ut_command Commtable[32];
static size_t Commcount;

int ut_dumppage(void *data, size_t sz)
{
	int r, i;
	char text[2048];
	size_t bufsz;

	bufsz = 2048;

	r = 0;

	for (i = 0; i < sz; ++i) {
		r += snprintf(text + r, bufsz - r,
			" %02x", ((uint8_t *) data)[i]);
		if ((i + 1) % 16 == 0)
			r += snprintf(text + r, bufsz - r , "\n\r");
	}
	snprintf(text + r, bufsz - r, "\n\r");

	HAL_UART_Transmit(huart, (uint8_t *) text, strlen(text), 100);
	
	return 0;
}

static int parsecommand()
{
	int i;

	i = 0;

	Toks[i++] = strtok((char *) Rxdata, " ");

	while (i < MAXTOKS && (Toks[i++] = strtok(NULL, " ")) != NULL);

	return 0;
}

int ut_getcommand()
{
	if (HAL_UART_Receive_IT(huart, Rxdata + Rxoffset, 1)
		!= HAL_BUSY) {

		while (HAL_UART_GetState(huart) == HAL_UART_STATE_BUSY_RX);

		if (Rxoffset >= RXBUFSZ - 1 || Rxdata[Rxoffset] == '\r') {
			uint8_t s[2] = {'\r', '\n'};

			HAL_UART_Transmit(huart, s, 2, 100);

			if (Rxdata[Rxoffset] == '\r')
				Rxdata[Rxoffset] = '\0';

			Rxdata[Rxoffset + 1] = '\0';
			Rxoffset = 0;
			Gotcommand = 1;
		}
		else if (isprint(Rxdata[Rxoffset])) {
			HAL_UART_Transmit(huart, Rxdata + Rxoffset, 1, 100);

			++Rxoffset;
		}
		else if (Rxdata[Rxoffset] == 0x08 && Rxoffset > 0) {
			uint8_t buf[3] = {0x08, ' ', 0x08};

			HAL_UART_Transmit(huart, buf, 3, 100);

			--Rxoffset;
		}
	}

	if (!Gotcommand)
		return 1;

	parsecommand();

	return 0;
}

int ut_promptcommand()
{
	char s[256];

	Gotcommand = 0;

	sprintf(s, "enter comand: ");

	HAL_UART_Transmit(huart, (uint8_t *) s, strlen(s), 100);

	return 0;
}

int ut_write(const char *format, ...)
{
	char buf[1024];
	va_list args;

	va_start(args, format);

	vsprintf(buf, format, args);

	HAL_UART_Transmit(huart, (uint8_t *) buf, strlen(buf), 100);

	return 0;
}

int ut_init(UART_HandleTypeDef *hu)
{
	huart = hu;

	Commcount = 0;

	return 0;
}

int ut_addcommand(const char *name, int (*func)(const char **))
{
	Commtable[Commcount].name = name;
	Commtable[Commcount].func = func;

	++Commcount;

	return 0;
}

int ut_executecommand()
{
	char s[256];
	int i;

	for (i = 0; i < Commcount; ++i) {
		if (strcmp(Toks[0], Commtable[i].name) == 0) {
			Commtable[i].func((const char **) Toks);
			return 0;
		}
	}

	if (strlen(Toks[0]) != 0) {
		sprintf(s, "unknown command\n\r");
		HAL_UART_Transmit(huart, (uint8_t *) s, strlen(s), 100);
	}

	return 0;
}

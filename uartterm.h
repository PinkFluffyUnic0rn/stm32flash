#ifndef UARTTERM_H
#define UARTTERM_H

int ut_dumppage(uint8_t *data, char *text, size_t sz);

int ut_getcommand();

int ut_promptcommand();

int ut_init(UART_HandleTypeDef *hu);

int ut_addcommand(const char *name, int (*func)(const char **));

int ut_executecommand();

#endif

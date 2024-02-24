#ifndef __USDELAY_H
#define __USDELAY_H

#include "main.h"

extern TIM_HandleTypeDef htim1;

#define USDELAY(us)					\
do {							\
	__HAL_TIM_SET_COUNTER(&htim1, 0);		\
	while (__HAL_TIM_GET_COUNTER(&htim1) < us);	\
} while (0);

#endif

/*
 * adg739_driver.h
 *
 *  Created on: 23.04.2012
 *      Author: korrav
 */

#ifndef ADG739_DRIVER_H_
#define ADG739_DRIVER_H_

#include <linux/ioctl.h>

//КОМАНДЫ IOCTL
#define ID_IO_ADG739 202	//идентификатор для команд ioctl

//ПАРАМЕТРЫ КОМАНДЫ WRITE
#define ARG_W_MULTIPLEXER_SIGNAL 's'      //конфигурирование мультиплексора для передачи сигнала гидрофона
#define ARG_W_MULTIPLEXER_GND 	 'g'      //конфигурирование мультиплексора для передачи нулевой разности потенциалов между выводами (КАЛИБРОВКА)
#define ARG_W_MULTIPLEXER_VBUS 	 'v'      /*конфигурирование мультиплексора для передачи разности потенциалов,
										  равной полному диапазону значений АЦП, между выводами (КАЛИБРОВКА)*/

//ПОЛЬЗАВАТЕЛЬСКИЕ НАСТРОЙКИ
#define MULTIPLEXER_NAME "adg739"	//имя узла мультиплексора устройства
#define NUM_MULTIPLEXER 4	//количество микросхем мультиплексоров на плате
#define DEV_CLASS_MUL "MAD - multiplexer"	//имя класса, к которому принадлежат устройства акустического модуля

#endif /* ADG739_DRIVER_H_ */

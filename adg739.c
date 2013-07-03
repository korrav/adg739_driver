/*
 * adg739_driver.cpp
 *
 *  Created on: 23.04.2012
 *      Author: korrav
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <mach/mux.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <asm/string.h>
#include <asm/gpio.h>	//перед компиляцией изменить на <asm/gpio.h>
#include <asm/uaccess.h>
#include "adg739.h"
MODULE_LICENSE( "GPL" );
MODULE_AUTHOR( "Andrej Korobchenko <korrav@yandex.ru>" );
MODULE_VERSION( "0:1.0" );

static char device_name[]=MULTIPLEXER_NAME; //имя устройства
dev_t dev_adg739; //содержит старший и младший номер устройства
static struct cdev cdev_adg739;
static struct class* devclass;	//класс устройств, к которому принадлежит pga2500
static DEFINE_MUTEX(device_lockk);	//мьютекс, применяемый для блокировки критически важного сегмента кода

struct adg739_data {	//специфичная для драйвера adg739 структура
	dev_t devt;
	struct spi_device* spi;
	spinlock_t	spi_lock;				    //спин-блокировка
	unsigned users;					    //количество процессов, использующих сейчас данное устройство
	char buffer[2 * NUM_MULTIPLEXER];		//буфер, учавствующий в передаче данных драйверу контроллера spi, а также хранящий текущее состояние регистров мультиплексоров
} *adg739_status;

//ФУНКЦИИ СТРУКТУРЫ FILE_OPERATIONS

static int adg739_open(struct inode *inode, struct file *filp)
{
	spin_lock(&adg739_status->spi_lock);
	filp->private_data = adg739_status;
	adg739_status->users++;
	spin_unlock(&adg739_status->spi_lock);
	nonseekable_open(inode, filp);	//сообщение ядру, что данное устройство не поддерживает произвольный доступ к данным
	return (0);

}

static int adg739_release (struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	mutex_lock(&device_lockk);
	adg739_status->users--;
	mutex_unlock(&device_lockk);
	return (0);
}

static void adg739_complete(void *arg)	//функция обратного вызова по окончанию обработки сообщения контроллером spi
{
	complete(arg);
}

static ssize_t adg739_write(struct file *filp, const char __user *buf, size_t count, loff_t *fpos)
{
	char buf_term[NUM_MULTIPLEXER];	//буфер, куда копируются сообщения из пользовательского пространства, и где они проходят предварительное форматирование
	int status = 0;
	int i =0;
	struct spi_transfer t = {		//формируется передача
			.tx_buf = adg739_status->buffer,
			.len = NUM_MULTIPLEXER * 2,
	};
	struct spi_message	m;	// сообщение
	DECLARE_COMPLETION_ONSTACK(done);	//объявляется и инициализуется условная переменная
	//проверка на достоверность переданного буфера
	if (count > NUM_MULTIPLEXER)
		return (-EMSGSIZE);
	if (copy_from_user(buf_term, buf, count))
		return (-EFAULT);
	for (i=0; i<count; i++)
	{
		switch(buf_term[i])
		{
		case 's':
			buf_term[i] = 0x11;
			break;
		case 'v':
			buf_term[i] = 0x82;
			break;
		case 'g':
			buf_term[i] = 0x88;
			break;
		default:
			return (-EINVAL);
		}
	}
	//передача сообщения драйверу контроллера
	mutex_lock(&device_lockk);
	for (i=0; i<count; i++) {
		adg739_status->buffer[i]= buf_term[i];
		adg739_status->buffer[i+4]= buf_term[i];
	}

	spi_message_init(&m);	//инициализация сообщения
	spi_message_add_tail(&t, &m);	//постановка передачи в очередь сообщения
	m.complete = adg739_complete;
	m.context = &done;
	if (adg739_status->spi == NULL)
		status = -ESHUTDOWN;
	else
	{
		status = spi_async(adg739_status->spi, &m);	//передача сообщения
		printk(KERN_INFO "Status function spi_async = %d\n", status);	
	}
	if (status == 0) {
		wait_for_completion(&done);	//ожидание обработки сообщения контроллером spi
		status = m.status;
		printk(KERN_INFO "Status message = %d\n", status);
		if (status == 0)
			status = m.actual_length/2;
	}
	mutex_unlock(&device_lockk);
	return (status);
}

//ФУНКЦИИ СТРУКТУРЫ SPI_DRIVER

static int	__devinit adg739_probe(struct spi_device *spi)
{
	int status, dev;
	//регистрация устройства
	dev =device_create(devclass, &spi->dev, dev_adg739, NULL, MULTIPLEXER_NAME);	//создание устройства
	status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	if(status != 0)
	{
		printk(KERN_ERR "The device_create function failed\n");
		return (status);
	}
	//инициализация членов структуры состояния драйвера
	mutex_lock(&device_lockk);
	adg739_status->users = 0;
	adg739_status->spi = spi;
	spi->bits_per_word = 16;
	spi->max_speed_hz = 700000;
	spin_lock_init(&adg739_status->spi_lock);
	memset(adg739_status->buffer, 0, sizeof(adg739_status->buffer));
	spi_set_drvdata(spi, adg739_status);	//присваевает указателю spi->dev->driver_data значение adg739_status
	mutex_unlock(&device_lockk);
	return (0);
}

static int __devexit adg739_remove(struct spi_device *spi)
{
	mutex_lock(&device_lockk);
	adg739_status->spi = NULL;
	spi_set_drvdata(spi, NULL);
	device_destroy(devclass, dev_adg739);
	mutex_unlock(&device_lockk);
	return (0);
}

//СТРУКТУРА FILE_OPERATIONS
static  const struct file_operations adg739_fops= {
		.owner = THIS_MODULE,
		.open = adg739_open,			//open
		.release = adg739_release,		//release
		.write = adg739_write,			//write
};

//СТРУКТУРА SPI_DRIVER
struct spi_driver spi_adg739_driver = {
		.driver =
		{
				.name = MULTIPLEXER_NAME,
				.owner = THIS_MODULE,
		},
		.probe = adg739_probe,
		.remove = adg739_remove,

};

//ФУНКЦИИ ИНИЦИАЛИЗАЦИИ И ВЫКЛЮЧЕНИЯ МОДУЛЯ ДРАЙВЕРА

static int __init adg739_init(void)
{
	//выделение памяти для структуры состояния драйвера
	adg739_status = kzalloc(sizeof(struct adg739_data), GFP_KERNEL);
	if (!adg739_status)
	{
		return (-ENOMEM);
	}
	//получение идентификатора для устройства
	if(alloc_chrdev_region(&dev_adg739, 0, 1, device_name))
	{
		printk(KERN_ALERT "The request_mem_region function failed\n");
		return (1);
	}

	//регистрация символьного устройства
	cdev_init(&cdev_adg739, &adg739_fops);
	cdev_adg739.owner = THIS_MODULE;
	if (cdev_add(&cdev_adg739, dev_adg739, 1))
	{
		unregister_chrdev_region(dev_adg739, 1);
		printk(KERN_ERR "The cdev_add function failed\n");
		return(1);
	}

	//регистрация класса устройств
	devclass = class_create( THIS_MODULE, DEV_CLASS_MUL);	//создание класса
	if (IS_ERR(devclass))
	{
		printk(KERN_ERR "The class_create function failed\n");
		unregister_chrdev_region(dev_adg739, 1);
		return (PTR_ERR(devclass));
	}

	//регистрация spi драйвера
	if (spi_register_driver(&spi_adg739_driver))
	{
		printk(KERN_ERR "The spi_register_driver function failed\n");
		unregister_chrdev_region(dev_adg739, 1);
		class_destroy(devclass);
		return (1);
	}
	return (0);
}

module_init(adg739_init);

static void __exit adg739_exit(void)
{
	kfree(adg739_status);
	spi_unregister_driver(&spi_adg739_driver);
	class_destroy(devclass);
	unregister_chrdev_region(dev_adg739, 1);
}
module_exit(adg739_exit);


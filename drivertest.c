#include <os.h>
#include <mini-os/os.h>
#include <xmalloc.h>
#include <console.h>
#include <lwip/api.h>
#include <mini-os/xenbus.h>

#include <mini-os/os.h>
#include <mini-os/xenbus.h>
#include <mini-os/events.h>
#include <errno.h>
#include <xen/io/netif.h>
#include <mini-os/gnttab.h>
#include <mini-os/xmalloc.h>
#include <mini-os/time.h>
#include <mini-os/netfront.h>
#include <mini-os/lib.h>
#include <mini-os/semaphore.h>
#include <vgpiofront.h>



// Leave these here!! They are required by mini-os
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long t0, start_ts, end_ts;
int running = 1;
uint64_t us, ns;

struct vgpiofront_dev *gpio_dev;
unsigned int gpioLED = 403;       		// Linux 403 = Up 37
unsigned int gpioButton = 404;       	// Linux 464 = Up 31
unsigned int gpioIn = 464;       		// Linux 464 = Up 27
unsigned int gpioOut = 430;				// Linux 430 = Up 29

static void irq_handler(void);
static void button_handler(void);

static void irq_handler()
{
	end_ts = NOW();
	us = (start_ts-t0)/1000;
    ns = (start_ts-t0) - (us*1000);
	tprintk("set_value took %lu,%luus\n", us,ns);
	us = (end_ts - start_ts)/1000;
    ns = (end_ts - start_ts) - (us*1000);
    tprintk("interrupt after %lu,%luus\n", us,ns);
    us = (end_ts - t0)/1000;
    ns = (end_ts - t0) - (us*1000);
    tprintk("total: %lu,%luus\n", us,ns);
 
}

static void button_handler()
{
	tprintk("Button pressed!\n");
}

void run_client(void *p)
{
	//~ uint64_t t1 = 0, t2 = 0, us = 0, ns = 0;
	int ret = 0;
	unsigned int ledOn = 0;
	
    tprintk("Drivertest!\n");
        
 
	gpio_dev = init_vgpiofront("device/vgpio/0");
	
	ret = gpio_request(gpio_dev, gpioLED, NULL);
	tprintk("gpio_request (pin %d): %d\n",gpioLED, ret);
	
	ret = gpio_request(gpio_dev, gpioButton, NULL);
	tprintk("button gpio_request (pin %d): %d\n",gpioButton, ret);
	
	ret = gpio_request(gpio_dev, gpioIn, NULL);
	tprintk("button gpio_request (pin %d): %d\n",gpioIn, ret);
	
	ret = gpio_request(gpio_dev, gpioOut, NULL);
	tprintk("button gpio_request (pin %d): %d\n",gpioOut, ret);
	
	ret = gpio_direction_input(gpio_dev, gpioButton);
	tprintk("gpio_direction_input (pin %d): %d\n",gpioButton, ret);
	
	ret = gpio_direction_input(gpio_dev, gpioIn);
	tprintk("gpio_direction_input (pin %d): %d\n",gpioIn, ret);
	
	ret = gpio_direction_output(gpio_dev, gpioOut, 1);
	tprintk("gpio_direction_output (pin %d): %d\n",gpioOut, ret);
	
	ret = gpio_request_irq(gpio_dev, gpioButton, button_handler, IRQF_TRIGGER_FALLING);
	tprintk("irq request (pin %d): %d\n",gpioButton, ret);
	
	ret = gpio_request_irq(gpio_dev, gpioIn, irq_handler, IRQF_TRIGGER_FALLING);
	tprintk("irq request (pin %d): %d\n",gpioIn, ret);
	
	msleep(1000);
	
	t0 = NOW();
	gpio_set_value(gpio_dev, gpioOut, 0);
	start_ts = NOW();
	
    while (running == 1) {
		//~ t1 = NOW();
		gpio_set_value(gpio_dev, gpioLED, ledOn);
		gpio_set_value(gpio_dev, gpioOut, 1);
        ledOn = !ledOn;
        msleep(1000);
        t0 = NOW();
		gpio_set_value(gpio_dev, gpioOut, 0);
		start_ts = NOW();
        //~ msleep(1500);
        //~ vgpiofront_send(gpio2);
    }
    shutdown_vgpiofront(gpio_dev);
    do_exit();
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
	gpio_free_irq(gpio_dev, gpioButton);
	gpio_free_irq(gpio_dev, gpioIn);
	gpio_free(gpio_dev, gpioLED);
	gpio_free(gpio_dev, gpioButton);
	gpio_free(gpio_dev, gpioIn);
	gpio_free(gpio_dev, gpioOut);
	shutdown_vgpiofront(gpio_dev);
	//~ shutdown_vgpiofront(gpio2);
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("Drivertest is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    create_thread("drivertest", run_client, NULL);
     
    return 0;
}



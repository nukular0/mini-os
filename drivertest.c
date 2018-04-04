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
unsigned long start_ts, end_ts;
int running = 1;

struct vgpiofront_dev *gpio_dev;
unsigned int gpioLED = 403;       		// Linux 403 = Up 37
unsigned int gpioFan = 464;       		// Linux 464 = Up 27
unsigned int gpioButton = 404;       	// Linux 464 = Up 31

static void irq_handler(void);

static void irq_handler()
{
	tprintk("Interrupt!\n");
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
	
	//~ ret = gpio_request(gpio_dev, gpioLED, NULL);
	//~ tprintk("gpio_request (pin %d): %d\n",gpioLED, ret);
	
	//~ ret = gpio_request(gpio_dev, gpioLED, NULL);
	//~ tprintk("gpio_request (pin %d): %d\n",gpioLED, ret);
	
	ret = gpio_request(gpio_dev, gpioFan, NULL);
	tprintk("gpio_request (pin %d): %d\n",gpioFan, ret);
	
	ret = gpio_direction_output(gpio_dev, gpioLED, 0);
	tprintk("gpio_direction_output (pin %d): %d\n",gpioLED, ret);
	
	ret = gpio_direction_output(gpio_dev, gpioFan, 0);
	tprintk("gpio_direction_output (pin %d): %d\n",gpioFan, ret);
	
	gpio_set_value(gpio_dev, gpioFan, 1);
	
	//~ gpio_set_value(gpio_dev, gpioLED, 0);
	gpio_set_value(gpio_dev, gpioLED, 1);
	//~ gpio_set_value(gpio_dev, gpioLED, 0);
	//~ gpio_set_value(gpio_dev, gpioLED, 1);
	
	ret = gpio_request(gpio_dev, gpioButton, NULL);
	tprintk("button gpio_request (pin %d): %d\n",gpioButton, ret);
	
	ret = gpio_direction_input(gpio_dev, gpioButton);
	tprintk("gpio_direction_input (pin %d): %d\n",gpioButton, ret);
	
	//~ ret = gpio_set_debounce(gpio_dev, gpioButton, 200);
	//~ tprintk("gpio_set_debounce (pin %d): %d\n",gpioButton, ret);
	
	ret = gpio_request_irq(gpio_dev, gpioButton, irq_handler);
	tprintk("irq request (pin %d): %d\n",gpioButton, ret);
	
    while (running == 1) {
		//~ t1 = NOW();
		//~ gpio_set_value(gpio_dev, gpioLED, ledOn);
        //~ t2 = NOW();
        //~ us = (t2-t1)/1000;
        //~ ns = (t2-t1) - (us*1000);
        //~ tprintk("Elapsed: %lu,%luus\n", us,ns);
        ledOn = !ledOn;
                
        msleep(200);
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
	gpio_free(gpio_dev, gpioLED);
	gpio_free(gpio_dev, gpioFan);
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



#include <os.h>
#include <mini-os/os.h>
#include <xmalloc.h>
#include <console.h>
#include <mini-os/xenbus.h>

#include <netfront.h>
#include <lwip/api.h>

#include <mini-os/os.h>
#include <mini-os/xenbus.h>
#include <mini-os/events.h>
#include <errno.h>
#include <xen/io/netif.h>
#include <mini-os/gnttab.h>
#include <mini-os/xmalloc.h>
#include <mini-os/time.h>
#include <mini-os/lib.h>
#include <mini-os/semaphore.h>
#include <vgpiofront.h>


unsigned long start_ts, end_ts;

uint64_t ms, us, ns;

int sem = 0;
struct semaphore		irq_sema;

//~ struct vcanfront_dev *can_dev;
struct vgpiofront_dev *gpio_dev;
//~ unsigned int gpioLED = 403;       		// Linux 403 = Up 37
//~ unsigned int gpioButton = 404;       	// Linux 464 = Up 31
unsigned int gpioIn = 404;       		// Linux 464 = Up 27
unsigned int gpioOut = 464;				// Linux 430 = Up 29
unsigned int gpioOutDom0 = 430;				// Linux 430 = Up 29

static void irq_handler(void);




void print_time_diff(char* prefix, unsigned long t1, unsigned long t2)
{
	uint64_t ms, us, ns;
	ms = (t2-t1)/ (1000 * 1000);
	us = (t2-t1)/1000 - (ms * 1000);
	ns = (t2-t1) - (ms * 1000 * 1000) - (us*1000);
	printk("%s %lu,%03lu.%03luus\n",prefix, ms,us,ns);
}



static inline uint64_t get_tsc(void)
{
    uint64_t lo, hi;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) : : "%ecx" );
    return (hi << 32) + lo;
}



static void irq_handler()
{
	up(&irq_sema);
}


void eval_irq(void)
{
	gpio_request_irq(gpio_dev, gpioIn, &irq_handler, IRQF_TRIGGER_RISING);

	msleep(200);
	
	tprintk("eval IRQ\n\n");

	
	while(1){

		down(&irq_sema);
		gpio_set_value(gpio_dev, gpioOut, 0);     
		
		msleep(1);
		gpio_set_value(gpio_dev, gpioOut, 1);
				
	}
	
	

}

void run_client(void *p)
{

   
    
    tprintk("GPIO_EVAL VM started\n");

	init_SEMAPHORE(&irq_sema, 0);

	gpio_dev = init_vgpiofront(NULL);
	
	gpio_request(gpio_dev, gpioOut, NULL);
	gpio_request(gpio_dev, gpioOutDom0, NULL);
	gpio_request(gpio_dev, gpioIn, NULL);
		
	gpio_direction_output(gpio_dev, gpioOut, 1);
	gpio_direction_output(gpio_dev, gpioOutDom0, 1);
	gpio_direction_input(gpio_dev, gpioIn);
		
		
	eval_irq();
	
	msleep(400);

	
    do_exit();
}



#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{

    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("GPIO_EVAL is shutting down: %d\n", reason);
    stop_networking();
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    create_thread("GPIO_EVAL", run_client, NULL);
     
    return 0;
}



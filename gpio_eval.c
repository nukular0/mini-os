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


#define PIRQ 1
#define REPS 1000
#define FACTOR 1.1

// Leave these here!! They are required by mini-os
//~ unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long t0, start_ts, end_ts;

uint64_t ms, us, ns;

int sem = 0;
struct semaphore		irq_sema;

//~ struct vcanfront_dev *can_dev;
struct vgpiofront_dev *gpio_dev;
//~ unsigned int gpioLED = 403;       		// Linux 403 = Up 37
//~ unsigned int gpioButton = 404;       	// Linux 464 = Up 31
unsigned int gpioIn = 404;       		// Linux 464 = Up 27
unsigned int gpioOut = 430;				// Linux 430 = Up 29

static void irq_handler(void);
//~ static void button_handler(void);


uint64_t c_start[REPS], c_set[REPS], c_irq[REPS];
uint32_t rep = 0;

static void logTimes(void)
{
	uint64_t min = 0xFFFFFFFFFFFFFF, max = 0, avg = 0;
	unsigned i = 0;
	uint64_t cycles;
	
	printk("vm_c_start,vm_c_set,vm_c_irq,vm_c_diff_set_irq\n");
	for(; i < REPS; i++){
		cycles = c_irq[i] - c_set[i];
		if(cycles < min)
			min = cycles;
		if(cycles > max)
			max = cycles;
		avg += cycles;
		printk("%lu,%lu,%lu,%lu\n", c_start[i], c_set[i], c_irq[i],cycles);
		msleep(4);
	}
	
	
	avg /= REPS;
	msleep(200);
	printk("\nlogged %u elements: min %lu | max %lu | avg %lu\n", REPS, min, max, avg);
	msleep(100);
}


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
	c_irq[rep++] = get_tsc();
	up(&irq_sema);
}

static void pirq_handler(evtchn_port_t port, struct pt_regs *regs, void* data)
{
	printk("PIRQ!!!!\n");
}

void eval_irq(void)
{
	int i = 0;
	gpio_request_irq(gpio_dev, gpioIn, &irq_handler, IRQF_TRIGGER_RISING);

	msleep(200);
	
	tprintk("eval IRQ\n\n");

	
	for(; i < REPS; i++){

		c_start[rep] = get_tsc();
		gpio_set_value(gpio_dev, gpioOut, 1);     
		c_set[rep] = get_tsc();

		down(&irq_sema);

		gpio_set_value(gpio_dev, gpioOut, 0);
				
		mb();
	}
	
	logTimes();
	

}

static inline void eval_toggle(void)
{
	int set = 0;
	int i = 0;
	
	tprintk("eval TOGGLE\n\n");
	
	
	for(; i < REPS; i++){
		c_set[i] = get_tsc();
		gpio_set_value(gpio_dev, gpioOut, set);     
		c_irq[i] = get_tsc();

		set = !set;
	
		//~ cycles[rep++] = c_end - c_start;
		mb();
	}
	
	logTimes();

}

void run_client(void *p)
{

	int ret;

    
    
    tprintk("GPIO_EVAL VM started\n");

	init_SEMAPHORE(&irq_sema, 0);

	gpio_dev = init_vgpiofront(NULL);
	
	gpio_request(gpio_dev, gpioOut, NULL);
	gpio_request(gpio_dev, gpioIn, NULL);
		
	gpio_direction_output(gpio_dev, gpioOut, 0);
	gpio_direction_input(gpio_dev, gpioIn);
		
	ret = bind_pirq(PIRQ, 1, pirq_handler, NULL);
	if(ret) {
	 printk("Unabled to request irq: %u for use (error %d)\n", PIRQ, ret);
	}
		
		
	
		
	eval_irq();
	//~ eval_toggle();
	
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



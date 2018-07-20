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
//~ #include <vgpiofront.h>
//~ #include <vcanfront.h>



// Leave these here!! They are required by mini-os
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long t0, start_ts, end_ts;
int running = 1;
uint64_t ms, us, ns;

//~ struct vcanfront_dev *can_dev;
//~ struct vgpiofront_dev *gpio_dev;
//~ unsigned int gpioLED = 403;       		// Linux 403 = Up 37
//~ unsigned int gpioButton = 404;       	// Linux 464 = Up 31
//~ unsigned int gpioIn = 464;       		// Linux 464 = Up 27
//~ unsigned int gpioOut = 430;				// Linux 430 = Up 29

//~ static void irq_handler(void);
//~ static void button_handler(void);

//~ static void irq_handler()
//~ {
	//~ end_ts = NOW();
	//~ us = (start_ts-t0)/1000;
    //~ ns = (start_ts-t0) - (us*1000);
	//~ tprintk("set_value took %lu,%luus\n", us,ns);
	//~ us = (end_ts - start_ts)/1000;
    //~ ns = (end_ts - start_ts) - (us*1000);
    //~ tprintk("interrupt after %lu,%luus\n", us,ns);
    //~ us = (end_ts - t0)/1000;
    //~ ns = (end_ts - t0) - (us*1000);
    //~ tprintk("total: %lu,%luus\n", us,ns);
 
//~ }

//~ static void button_handler()
//~ {
	//~ tprintk("Button pressed!\n");
//~ }

//~ void print_can_frame(char* prefix, struct can_frame *cf)
//~ {
	//~ int i = 0;
	//~ tprintk("%s%3X [%d]", prefix, cf->can_id, cf->can_dlc);
	//~ for(; i < cf->can_dlc; i++){
		//~ printk(" %2X ", cf->data[i]);			
	//~ }
	//~ printk("\n");	
//~ }

//~ void can_rx_handler(struct can_frame *cf)
//~ {
	//~ end_ts = NOW();
	//~ print_can_frame("RX: ", cf);
	//~ ms = (end_ts-start_ts)/ (1000 * 1000);
	//~ us = (end_ts-start_ts)/1000 - (ms * 1000);
    //~ ns = (end_ts-start_ts) - (ms * 1000 * 1000) - (us*1000);
	//~ tprintk("RTT: %lu,%03lu.%lums\n", ms,us,ns);
//~ }


void run_client(void *p)
{

	//~ struct netconn *conn;
    //~ struct netbuf *nb;

    //~ struct ip_addr *addr;
    //~ unsigned short port;
    
    tprintk("Drivertest VM started\n");
    
    start_networking();    
	
    while (running == 1) {
		tprintk("Tick %lu\n", NOW());
		msleep(1000);
		
    }
    do_exit();
}



#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{

    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("Drivertest is shutting down: %d\n", reason);
    stop_networking();
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    create_thread("drivertest", run_client, NULL);
     
    return 0;
}



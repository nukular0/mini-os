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

struct vgpiofront_dev *gpio;
//~ struct vgpiofront_dev *gpio2;

void run_client(void *p)
{
	uint64_t t1 = 0, t2 = 0, us = 0, ns = 0;
	
	//~ struct netfront_dev* net;
    tprintk("Drivertest!\n");
    
 
	gpio = init_vgpiofront("device/vgpio/0");
	//~ gpio2 = init_vgpiofront("device/vgpio/1");
	//~ net = init_netfront(NULL, NULL, NULL, NULL);
    
	tprintk("Drivertest running!\n");
	
	
	
    while (running == 1) {
		t1 = NOW();
        vgpiofront_send(gpio);
        t2 = NOW();
        us = (t2-t1)/1000;
        ns = (t2-t1) - (us*1000);
        tprintk("Elapsed: %lu,%luus\n", us,ns);
        msleep(200);
        //~ msleep(1500);
        //~ vgpiofront_send(gpio2);
    }
    shutdown_vgpiofront(gpio);
    do_exit();
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
	shutdown_vgpiofront(gpio);
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



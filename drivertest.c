#include <os.h>
#include <mini-os/os.h>
#include <xmalloc.h>
#include <console.h>
//#include <netfront.h>
#include <lwip/api.h>

//#include <xen/xen.h>       /* We are doing something with Xen */
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
#include <mini-os/mydevicefront.h>



// Leave these here!! They are required by mini-os
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long start_ts, end_ts;
int running = 1;

void run_client(void *p)
{
	uint64_t t1 = 0, t2 = 0, us = 0, ns = 0;
	struct vgpiofront_dev* gpio;
	//~ struct netfront_dev* net;
    tprintk("Drivertest\n");
 
	gpio = init_vgpiofront("device/vgpio/0");
	//~ net = init_netfront(NULL, NULL, NULL, NULL);
    
	tprintk("Drivertest running!\n");
	
	shutdown_vgpiofront(gpio);
    while (running == 1) {
		t1 = NOW();
        t2 = NOW();
        us = (t2-t1)/1000;
        ns = (t2-t1) - (us*1000);
        tprintk("Elapsed: %lu,%luus\n", us,ns);
        msleep(10000);
        //vgpiofront_send(gpio);
    }
    
    
    
    do_exit();
}



int app_main(void *p)
{
    create_thread("drivertest", run_client, NULL);
     
    return 0;
}



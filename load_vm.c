#include <os.h>
#include <mini-os/os.h>
#include <xmalloc.h>
#include <console.h>
#include <mini-os/xenbus.h>

#include <mini-os/os.h>
#include <mini-os/xenbus.h>
#include <mini-os/events.h>
#include <errno.h>
//~ #include <xen/io/netif.h>
#include <mini-os/gnttab.h>
#include <mini-os/xmalloc.h>
#include <mini-os/time.h>
//~ #include <mini-os/netfront.h>
#include <mini-os/lib.h>
#include <mini-os/semaphore.h>



volatile uint64_t i = 0; 

void run_client(void *p)
{
	while(1){
		i *= 2;
	}
		
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("LoadVM is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    while(1){
		i = i+1;
	}
    create_thread("LoadVM", run_client, NULL);
     
    return 0;
}




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
#include <vcanfront.h>
#include <ma_eval.h>

#define TX_ID	0x123
#define RSP_ID	0x194

struct vcanfront_dev *can_dev;

struct semaphore	sem, rsp_sem;

struct can_frame tx_frame;
canid_t	my_can_id = 0x123;
uint64_t t1, t2, t3;

static inline uint64_t get_tsc(void)
{
    uint64_t lo, hi;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) : : "%ecx" );
    return (hi << 32) + lo;
}

void print_can_frame(char* prefix, struct can_frame *cf)
{
	int i = 0;
	tprintk("%s%3X [%d]", prefix, cf->can_id, cf->can_dlc);
	for(; i < cf->can_dlc; i++){
		printk(" %2X ", cf->data[i]);			
	}
	printk("\n");	
}

void print_time_diff(char* prefix, unsigned long t1, unsigned long t2, double factor)
{
	uint64_t diff, ms, us, ns;
	diff = t2-t1;
	
	if(factor != 1.0){
		diff /= factor;	
	}
	
	ms = (diff)/ (1000 * 1000);
	us = (diff)/1000 - (ms * 1000);
	ns = (diff) - (ms * 1000 * 1000) - (us*1000);
	tprintk("%s %lu,%03lu.%03lums\n",prefix, ms,us,ns);
}

uint64_t tscs_to_ns(unsigned long t1, unsigned long t2, double factor)
{
	uint64_t diff = t2-t1;
	
	if(factor != 1.0){
		diff /= factor;	
	}
	
	return diff;
}

void can_rx_handler(struct can_frame *cf)
{
	// TX_ID --> we are VM2/responder --> send response
	if(cf->can_id == TX_ID){
		vcanfront_send(can_dev, cf);
	}
	// RSP_ID --> we are VM1/sender --> up sema 
	else if(cf->can_id == RSP_ID){
		t2 = get_tsc();
		up(&sem);
	}
}



void run_client(void *p)
{
	int ret = 0;
	char *buf;
	
	int counter = 0;

    tprintk("CAN RTT bench app\n");

	can_dev = init_vcanfront("device/vcan/0");

	if(!can_dev)
		return;
		
	ret = vcanfront_register_rx_handler(can_dev, can_rx_handler);
	
	if(ret){
		tprintk("registering rx_handler failed (%d)\n", ret);
		return;
	}
	
	xenbus_read(XBT_NIL, "name", &buf);
	tprintk("My name is %s\n", buf);

	if(!strcmp("canvm1", buf)){
		my_can_id = 0x123;
	}
	else if(!strcmp("canvm2", buf)){
		my_can_id = 0x223;
	}
	
	tx_frame.can_dlc = 8;
	tx_frame.can_id = RSP_ID;
	
	free(buf);
	if(my_can_id == 0x223){
		while(1){
			msleep(5000);
		}
	}
		
	init_SEMAPHORE(&sem, 0);
	tx_frame.can_dlc = 8;
	tx_frame.can_id = TX_ID;

	printk("__START__\n");
	printk("RTT_IRQ[ns],overhead[ns]\n");
	
	msleep(10);
	
	while(counter++ < 10000){
		t1 = get_tsc();
		vcanfront_send(can_dev, &tx_frame);
		//~ print_can_frame("TX: ", &tx_frame);
		down(&sem);
		t3 = get_tsc();
		printk("%lu,%lu\n", tscs_to_ns(t1, t2, 1.089), tscs_to_ns(t2, t3, 1.089));
		msleep(2);
	}
	printk("__END__\n%u samples\n", counter);
	msleep(1000);
    shutdown_vcanfront(can_dev);
    do_exit();
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
	
	shutdown_vcanfront(can_dev);
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("CAN RTT benchmark is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    create_thread("CAN_RTT", run_client, NULL);
     
    return 0;
}

#ifdef EVAL
void resume_eval_vm(int rc)
{
	if(!rc){
		host = 1;
	}
}

void suspend_eval_vm(void)
{
	
}
#endif

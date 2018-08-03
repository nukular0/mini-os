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

#define TX_ID	0x123
#define RSP_ID	0x194

#define REPS 2000

#define US_TO_COUNTER(x) (x/(5.9733/1000))

#define CYCLES_NOW_START(low,high)	asm volatile ("CPUID\n\t"												\
			"RDTSC\n\t"																						\
			"mov %%edx, %0\n\t"																				\
			"mov %%eax, %1\n\t": "=r" (high), "=r" (low):: "%rax", "%rbx", "%rcx", "%rdx")		


#define CYCLES_NOW_END(low,high)	asm volatile ("RDTSCP\n\t" 										\
							"mov %%edx, %0\n\t" 													\
							"mov %%eax, %1\n\t"														\
							"CPUID\n\t": "=r" (high), "=r" (low):: "%rax", "%rbx", "%rcx", "%rdx")

// Leave these here!! They are required by mini-os
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long t0, start_ts, end_ts, start_ts2, end_ts2;
long diff;
int running = 1;

uint64_t rtt[REPS];
uint32_t rep = 0;

struct vcanfront_dev *can_dev;

struct can_frame tx_frame;
struct can_frame rsp_frame;

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
	if(cf->can_id == TX_ID){
		vcanfront_send(can_dev, &rsp_frame);
	}
	else if(cf->can_id == RSP_ID){
		end_ts = get_tsc();
		rtt[rep++] = tscs_to_ns(start_ts, end_ts, 1.1);
		//~ print_time_diff("RTT: ", start_ts, end_ts, 1.1);		
	}
	
	//~ if(rep % 1000 == 0){
		//~ tprintk("rep: %d\n", rep);
	//~ }
}


static void logTimes(void)
{
	uint64_t min = 0xFFFFFFFFFFFFFF, max = 0, avg = 0;
	unsigned i = 0;
	
	printk("\nrtt_ns\n");
	for(; i < rep; i++){
		if(rtt[i] < min)
			min = rtt[i];
		if(rtt[i] > max)
			max = rtt[i];
		avg += rtt[i];
		printk("%lu\n", rtt[i]);
		msleep(4);
	}
	
	
	avg /= rep;
	msleep(200);
	printk("\nlogged %u elements: min %lu | max %lu | avg %lu\n", REPS, min, max, avg);
	msleep(100);
}

void run_client(void *p)
{
	int ret = 0;
	char *buf;
	volatile unsigned int counter = 0; 


	tx_frame.can_id = TX_ID;
	tx_frame.can_dlc = 8;
	tx_frame.data[0] = 0x10;
	tx_frame.data[1] = 0x20;
	tx_frame.data[2] = 0x30;
	tx_frame.data[3] = 0x40;
	tx_frame.data[4] = 0x10;
	tx_frame.data[5] = 0x20;
	tx_frame.data[6] = 0x30;
	tx_frame.data[7] = 0x40;

	rsp_frame.can_id = RSP_ID;
	rsp_frame.can_dlc = 8;
	rsp_frame.data[0] = 0xA1;
	rsp_frame.data[1] = 0xA2;
	rsp_frame.data[2] = 0xA3;
	rsp_frame.data[3] = 0xA4;
	rsp_frame.data[4] = 0xA5;
	rsp_frame.data[5] = 0xA6;
	rsp_frame.data[6] = 0xA7;
	rsp_frame.data[7] = 0xA8;
	
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
	tprintk("my name is %s\n", buf);

	if(!strcmp("can_rtt_responder", buf)){
		tprintk("I am the can_rtt_responder\n");
	}
	else{
		tprintk("I am someone else\n");
		
	}
	
	start_ts = NOW();
	while(counter++ < US_TO_COUNTER(1000)){
	}
	end_ts = NOW();
	print_time_diff("counting to 100000 took ", start_ts, end_ts, 1);
	
	counter = 0;
	start_ts = NOW();
	while(counter++ < US_TO_COUNTER(2500)){
	}
	end_ts = NOW();
	print_time_diff("counting to 200000 took ", start_ts, end_ts, 1);

while(1)
{
	counter = 0;
	start_ts = get_tsc();
	while(counter++ < US_TO_COUNTER(5000)){
	}
	end_ts = get_tsc();
	print_time_diff("counting to 837053 took ", start_ts, end_ts, 1.089);
	msleep(500);
}
 
	free(buf);
		
	while(1){
		msleep(1000);
	}
		
	tprintk("starting benchmark\n");
		
    while(rep < REPS) {
		
		msleep(10);
		tx_frame.data[0]++;
		vcanfront_send(can_dev, &tx_frame);
		start_ts = get_tsc();
		//~ print_can_frame("TX: ", &tx_frame);
		
    }
    
    logTimes();
    
    shutdown_vcanfront(can_dev);
    do_exit();
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
	
	shutdown_vcanfront(can_dev);
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("CAN Test is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    create_thread("CAN_RTT", run_client, NULL);
     
    return 0;
}



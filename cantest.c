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

#define TX_ID	0x100
#define RSP_ID	0x200


#define CYCLES_NOW_START(low,high)	asm volatile ("CPUID\n\t"												\
			"RDTSC\n\t"																						\
			"mov %%edx, %0\n\t"																				\
			"mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low):: "%rax", "%rbx", "%rcx", "%rdx")		


#define CYCLES_NOW_END(low,high)	asm volatile ("RDTSCP\n\t" 										\
							"mov %%edx, %0\n\t" 													\
							"mov %%eax, %1\n\t"														\
							"CPUID\n\t": "=r" (high), "=r" (low):: "%rax", "%rbx", "%rcx", "%rdx")

// Leave these here!! They are required by mini-os
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long t0, start_ts, end_ts, start_ts2, end_ts2;
long diff;
int running = 1;


struct vcanfront_dev *can_dev;

struct can_frame tx_frame;
struct can_frame rsp_frame;


void print_can_frame(char* prefix, struct can_frame *cf)
{
	int i = 0;
	tprintk("%s%3X [%d]", prefix, cf->can_id, cf->can_dlc);
	for(; i < cf->can_dlc; i++){
		printk(" %2X ", cf->data[i]);			
	}
	printk("\n");	
}

void print_time_diff(char* prefix, unsigned long t1, unsigned long t2)
{
	uint64_t ms, us, ns;
	ms = (t2-t1)/ (1000 * 1000);
	us = (t2-t1)/1000 - (ms * 1000);
	ns = (t2-t1) - (ms * 1000 * 1000) - (us*1000);
	tprintk("%s %lu,%03lu.%03luus\n",prefix, ms,us,ns);
}
void can_rx_handler(struct can_frame *cf)
{
	if(cf->can_id == TX_ID){
		vcanfront_send(can_dev, &rsp_frame);
	}
	else if(cf->can_id == RSP_ID){
		CYCLES_NOW_END(cycles_low1, cycles_high1);
		end_ts2 = NOW();
        start_ts = ( ((unsigned long)cycles_high << 32) | cycles_low );
        end_ts = ( ((unsigned long)cycles_high1 << 32) | cycles_low1 );
		print_time_diff("RTT [ASM]", start_ts, end_ts);
		
		print_time_diff("RTT [NOW]", start_ts2, end_ts2);
		
		diff = (end_ts2-start_ts2) - (end_ts-start_ts);
		if(diff < 0)
			print_time_diff("diff:    ", (end_ts2-start_ts2), (end_ts-start_ts));
		else
			print_time_diff("diff:    ", (end_ts-start_ts), (end_ts2-start_ts2));
			
		
		
	}
	print_can_frame("RX: ", cf);
}


void run_client(void *p)
{
	int ret = 0;
	int i;
	

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
	
    tprintk("CAN test app!\n");
        
	can_dev = init_vcanfront("device/vcan/0");

	if(!can_dev)
		return;
		
	ret = vcanfront_register_rx_handler(can_dev, can_rx_handler);
	
	if(ret){
		tprintk("registering rx_handler failed (%d)\n", ret);
		return;
	}
	
	for(i = 0; i < 10;i++){
	
		CYCLES_NOW_START(cycles_low, cycles_high);
		CYCLES_NOW_END(cycles_low1, cycles_high1);
		start_ts = ( ((unsigned long)cycles_high << 32) | cycles_low );
		end_ts = ( ((unsigned long)cycles_high1 << 32) | cycles_low1 );

		print_time_diff("diff [ASM]", start_ts, end_ts);
		
		start_ts = NOW();
		end_ts = NOW();
		print_time_diff("diff [NOW]", start_ts, end_ts);
		
		printk("\n");
		msleep(20);
	}
	
    while (running == 1) {
		
		msleep(5);
		tx_frame.data[0]++;
		vcanfront_send(can_dev, &tx_frame);
		CYCLES_NOW_START(cycles_low, cycles_high);
		start_ts2 = NOW();
		print_can_frame("TX: ", &tx_frame);
    }
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
    create_thread("CANtest", run_client, NULL);
     
    return 0;
}



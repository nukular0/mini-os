
#include <os.h>
#include <mini-os/os.h>
#include <xmalloc.h>
#include <console.h>
#include <mini-os/xenbus.h>

#include <mini-os/os.h>
#include <mini-os/xenbus.h>
#include <mini-os/events.h>
#include <errno.h>

#include <mini-os/gnttab.h>
#include <mini-os/xmalloc.h>
#include <mini-os/time.h>

#include <mini-os/lib.h>
#include <mini-os/semaphore.h>
#include <vcanfront.h>

#define NUM_SAMPLES 10000

#define XS_PATH "data/bench"

int sample_counter = 0;
uint64_t _t1[NUM_SAMPLES];
uint64_t _t2[NUM_SAMPLES];


static void dump_eval_data(void){
	int i = 0;
	
	printk("__START__\n");
	printk("t1[ns],t2[ns]\n");
	for(; i < sample_counter; i++){
		printk("%lu,%lu\n", _t1[i], _t2[i]);
		msleep(5);
	}
	printk("__END__\n%u samples\n", sample_counter);

	msleep(1000);
}


struct semaphore		sem;



static inline uint64_t get_tsc(void)
{
    uint64_t lo, hi;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) : : "%ecx" );
    return (hi << 32) + lo;
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


int read_var;
uint64_t t1, t2, t3;
uint64_t t4;

void write_thread(void* p)
{
	volatile int i = 0;
	tprintk("write_thread started\n");
	msleep(200);
	while(i++ < NUM_SAMPLES){
		//~ tprintk("Writing to Xenstore: %d\n", i);
		t1 = NOW();
		xenbus_printf(XBT_NIL, "/sensors", "test","%lu", t1);
		t2 = NOW();
		//~ printk("2");
		print_time_diff("write_time:           ", t1, t2, 1);

		_t1[sample_counter] = t1;
		_t2[sample_counter] = t2;
		sample_counter++;

		msleep(5);
	}
	
	dump_eval_data();		

	msleep(3);
	do_exit();
}
	

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
	
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("Xenstore benchmark is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    create_thread("XSBW", write_thread, NULL);
     
    return 0;
}

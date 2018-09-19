
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
uint64_t write_time[NUM_SAMPLES];
uint64_t watch_latency[NUM_SAMPLES];
uint64_t _t3[NUM_SAMPLES];


void dump_eval_data(void){
	int i = 0;
	
	printk("__START__\n");
	printk("t3[ns]\n");
	for(; i < sample_counter; i++){
		printk("%lu\n", _t3[i]);
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

void run_client(void *p)
{
	
	xenbus_event_queue events = NULL;
	char *err;
	char *s;
	
	//~ double factor = 1.089;
	
	xenbus_printf(XBT_NIL, "/sensors", "test","%d", 0);
	
	/*Setup the watch to wait for the backend */
	if((err = xenbus_watch_path_token(XBT_NIL, "/sensors", "test", &events))) {
	  printk("Could not set a watch on %s, error was %s\n", "data/test", err);
	  free(err);
	  return;
	}
	
	msleep(100);
	
	
	while(1){		
				
		xenbus_wait_for_watch(&events);

		t3 = NOW();
		xenbus_read(XBT_NIL, "/sensors/test", &s);

		//~ tprintk("read from XS: %s\n", s);
		free(s);
		
		sscanf(s, "%lu", &t1);
		//~ tprintk("t1: %lu\n", t1);

		if(!t1)
			continue;
		
			
		//~ print_time_diff("watch latency:        ", t1, t3, 1.089);
		
		//~ write_time[sample_counter] 		= ((double)(t2-t1))/factor;
		//~ watch_latency[sample_counter] 	= ((double)(t3-t1))/factor;
		_t3[sample_counter] 		= t3;
		sample_counter++;
			
		//~ msleep(5);
	}
	
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
	
	dump_eval_data();
	
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("Xenstore benchmark is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    create_thread("XSB", run_client, NULL);
     
    return 0;
}


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

#define NUM_SAMPLES 10

#define XS_PATH "data/bench"

int sample_counter = 0;
uint64_t write_time[NUM_SAMPLES];
uint64_t watch_latency[NUM_SAMPLES];
uint64_t control[NUM_SAMPLES];


static void dump_eval_data(void){
	int i = 0;
	
	printk("__START__\n");
	printk("write_time[ns],watch_latency[ns],control[i]\n");
	for(; i < sample_counter; i++){
		printk("%lu,%lu,%lu\n", write_time[i], watch_latency[i], control[i]);
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
		tprintk("Writing to Xenstore: %d\n", i);
		t1 = get_tsc();
		xenbus_printf(XBT_NIL, "/sensors", "test","%d", i);
		t2 = get_tsc();
		//~ printk("2");
		
		down(&sem);
		msleep(3);
	}
	
	dump_eval_data();		

	msleep(3);
	do_exit();
}
	
void run_client(void *p)
{
	
	xenbus_event_queue events = NULL;
	char *err;
	int err_counter = 0;
	
	double factor = 1.089;
	
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

		t3 = get_tsc();
		//~ printk("3");
		t4 = get_tsc();
		read_var = xenbus_read_integer("/sensors/test");

		tprintk("read from XS: %d\n", read_var);

		if(!read_var)
			continue;
		
		if( (t2-t1) > MILLISECS(5)){
			print_time_diff("write time: ", t1, t2, 1);
			err_counter++;
		}
		
		
		print_time_diff("write_time:           ", t1, t2, 1.089);
		print_time_diff("watch latency:        ", t1, t3, 1.089);
		print_time_diff("control:              ", t3, t4, 1.089);
		
		write_time[sample_counter] 		= ((double)(t2-t1))/factor;
		watch_latency[sample_counter] 	= ((double)(t3-t1))/factor;
		control[sample_counter] 		= ((double)(t4-t3))/factor;
		sample_counter++;
		
		up(&sem);
	
		//~ msleep(5);
	}
	
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
    create_thread("XSB", run_client, NULL);
    create_thread("XSBW", write_thread, NULL);
     
    return 0;
}

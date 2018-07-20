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


#define CYCLES_NOW_START(low,high)	asm volatile ("CPUID\n\t"												\
			"RDTSC\n\t"																						\
			"mov %%edx, %0\n\t"																				\
			"mov %%eax, %1\n\t": "=r" (high), "=r" (low):: "%rax", "%rbx", "%rcx", "%rdx")		


#define CYCLES_NOW_END(low,high)	asm volatile ("RDTSCP\n\t" 										\
							"mov %%edx, %0\n\t" 													\
							"mov %%eax, %1\n\t"														\
							"CPUID\n\t": "=r" (high), "=r" (low):: "%rax", "%rbx", "%rcx", "%rdx")

#define XENSTORE_SENSOR_PATH "/sensors"
#define TO_INT16(x, y) ((int16_t)( (y << 8) + x))


unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long t0, start_ts, end_ts, start_ts2, end_ts2;
long diff;
int running = 1;

void print_time_diff(unsigned long t1, unsigned long t2)
{
	uint64_t ms, us, ns;
	ms = (t2-t1)/ (1000 * 1000);
	us = (t2-t1)/1000 - (ms * 1000);
	ns = (t2-t1) - (ms * 1000 * 1000) - (us*1000);
	printk("%lu,%lu %03lu.%03lu\n",t2-t1, ms,us,ns);
}

void run_client(void *p)
{
	xenbus_event_queue events = NULL;
	const char *path = "/mytest/timing/start";
	char *err;
	char *value;
	
    tprintk("Xenstore Latency Benchmark!\n");
        
    /*Setup the watch to wait for data in /sensors/timing/start */
	
	if((err = xenbus_watch_path_token(XBT_NIL, path, path, &events))) {
	  tprintk("Could not set a watch on %s, error was %s\n", path, err);
	  free(err);
	  return;
	}
	
	while(1){
		xenbus_wait_for_watch(&events);
		CYCLES_NOW_END(cycles_low1, cycles_high1);
		
		if((err = xenbus_read(XBT_NIL, path, &value))) {
			tprintk("Unable to read %s during vgpiofront initialization! error = %s\n", path, err);
			free(err);
			continue;
		}
		if(sscanf(value, "%lu", &start_ts) != 1) {
			tprintk("%s has non-integer value (%s)\n", path, value);
			free(value);
			continue;
		}
		free(value);
		
		end_ts = ( ((uint64_t)cycles_high1 << 32) | cycles_low1 );
		
		print_time_diff(start_ts, end_ts);
	}
	
    do_exit();
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("Xenstore Latency Benchmark is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

int app_main(void *p)
{
    
    create_thread("Xenstore Latency Benchmark", run_client, NULL);
         
    return 0;
}



#include <os.h>
#include <mini-os/os.h>
#include <xmalloc.h>
#include <console.h>
#include <mini-os/xenbus.h>
#include <netfront.h>
#include <lwip/api.h>
#include <mini-os/os.h>
#include <mini-os/xenbus.h>
#include <mini-os/events.h>
#include <errno.h>
#include <mini-os/gnttab.h>
#include <mini-os/xmalloc.h>
#include <mini-os/time.h>
#include <mini-os/lib.h>
#include <mini-os/semaphore.h>
#include <mini-os/vcanfront.h>
#include <mini-os/can.h>
#include <mini-os/can_error.h>
#include <mini-os/time.h>
#include <mini-os/headlight_vm.h>

#include <inttypes.h>
#include <mini-os/os.h>
#include <mini-os/mm.h>
#include <mini-os/traps.h>
#include <mini-os/lib.h>
#include <mini-os/xenbus.h>
#include <mini-os/events.h>
#include <mini-os/errno.h>
#include <mini-os/sched.h>
#include <mini-os/wait.h>
#include <xen/io/xs_wire.h>
#include <xen/hvm/params.h>
#include <mini-os/spinlock.h>
#include <mini-os/xmalloc.h>
#include <ma_eval.h>


#define MSG_PERIOD_MS	10

#ifdef EVAL
uint64_t req_chkpt_time[NUM_SAMPLES];
uint64_t pkt_send_time[NUM_SAMPLES];
uint32_t req_chkpt_sample_counter = 0;

int run_eval = 0;

static void dump_eval_data(void){
	int i = 0;
	
	printk("__START__\n");
	printk("req_chkpt_time[ns],time_suspended[ns],t_pkt_send[ns]\n");
	for(; i < req_chkpt_sample_counter; i++){
		printk("%lu,%lu,%lu\n", req_chkpt_time[i], time_suspended[i], pkt_send_time[i]);
		msleep(5);
	}
	printk("__END__\n%u samples\n", req_chkpt_sample_counter);
	
	run_eval = 0;
	msleep(1000);
}



#endif


struct vcanfront_dev 	*can_dev;
int16_t lwr_val = 0x07FF, glw_val = 0, afs_val = 0x07FF;
struct can_frame lwr_frame, glw_frame, afs_frame;

struct thread *app_thread, *tick_thread;

static inline uint64_t get_tsc(void)
{
    uint64_t lo, hi;
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) : : "%ecx" );
    return (hi << 32) + lo;
}

void init_lwr(void){
	tprintk("init_lwr()...");
	int i = 0;
	for(; i < 3; i++){
		lwr_frame.data[4] = 0x20;
		
		vcanfront_send(can_dev, &lwr_frame);		
		msleep(10);
	}
	printk("done!\n");
}

void init_afs(void){
	tprintk("init_lwr()...");
	int i = 0;
	for(; i < 3; i++){
		afs_frame.data[4] = 0x20;		
		
		vcanfront_send(can_dev, &afs_frame);	
		msleep(10);	
	}
	printk("done!\n");
}

#ifdef EVAL
void can_rx_handler(struct can_frame *cf)
{
	if(cf->can_id == 0x666){
		printk("Eval start\n");
		run_eval = 1;
	}
}
#endif


void run_client(void *p)
{
	int remaining_sleep = MSG_PERIOD_MS;
#ifdef EVAL
	uint64_t t_pkt_send;
#endif	
	uint64_t t_start, req_time;
	
	memset(&lwr_frame, 0, sizeof(struct can_frame));
	memset(&glw_frame, 0, sizeof(struct can_frame));
	memset(&afs_frame, 0, sizeof(struct can_frame));

	lwr_frame.can_id = 0x190;
	lwr_frame.can_dlc = 8;
	
	glw_frame.can_id = 0x110;
	glw_frame.can_dlc = 8;
	
	afs_frame.can_id = 0x192;
	afs_frame.can_dlc = 8;

	start_networking();    
	
    tprintk("Headlight VM started!\n");
        
	can_dev = init_vcanfront("device/vcan/0");

	if(!can_dev)
		return;
#ifdef EVAL
	vcanfront_register_rx_handler(can_dev, can_rx_handler);
#endif	

	init_afs();
	init_lwr();

	while(1){
		
		// LWR is for up and down
		lwr_frame.data[0] = lwr_val;
		lwr_frame.data[1] = (lwr_val >> 8);
		
		lwr_frame.data[3] = lwr_frame.data[0];
		lwr_frame.data[4] = lwr_frame.data[1];

#ifdef EVAL
		t_pkt_send = NOW();
#endif
		vcanfront_send(can_dev, &lwr_frame);
		
		checkpoint_requested = 0;
		t_start = NOW();
		request_checkpoint();
		req_time = NOW() - t_start;

	
#ifdef EVAL
		if(run_eval){
			if(req_chkpt_sample_counter < NUM_SAMPLES){
				pkt_send_time[req_chkpt_sample_counter] = t_pkt_send;
				req_chkpt_time[req_chkpt_sample_counter++] = req_time;
			}
			if(req_chkpt_sample_counter >= NUM_SAMPLES){
				dump_eval_data();
			}
		}
#endif		
		//~ if(NSEC_TO_MSEC(req_time) > 15){
		printk("checkpointing took %lums\n", NSEC_TO_USEC(req_time));
		//~ } 
		
		// AFS is for left and right
		
		//~ msleep(4);
		//~ afs_frame.data[0] = afs_val;
		//~ afs_frame.data[1] = (afs_val >> 8);
		//~ afs_frame.data[3] = afs_frame.data[0];
		//~ afs_frame.data[4] = afs_frame.data[1];
		//~ vcanfront_send(can_dev, &afs_frame);
		
		
		// (requesting) checkpointing takes time, so we calculate the actual
		// time we have to sleep to achieve a period of MSG_PERIOD_MS
		remaining_sleep = MSG_PERIOD_MS - NSEC_TO_MSEC(req_time);
		//~ remaining_sleep = 7;
		
		/* the calculation above (end_ts-start_ts) gives wrong results 
		*  (e.g. very large numbers) sometimes for some reason , 
		*  so let's do a santiy check 
		*/
		if(remaining_sleep > MSG_PERIOD_MS){
			remaining_sleep = MSG_PERIOD_MS;
		}
		
		else if(remaining_sleep <= 0){
			continue;
		}
			
		msleep(remaining_sleep);

	}
	
    shutdown_vcanfront(can_dev);
    do_exit();
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
	
	shutdown_vcanfront(can_dev);
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("Headlight VM is shutting down: %d\n", reason);
    stop_networking();
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

void lwr_thread(void *p)
{
	int lwr_delta = 100;
	int min = 1024;
	int max = 3072;

	uint64_t t1, delta;
	msleep(3000);
	while(1){
		lwr_val += lwr_delta;
		//~ afs_val += lwr_delta;
		//~ glw_val += lwr_delta;
		//~ glw_val = afs_val = lwr_val;
		
		if(lwr_val >= max || lwr_val <= min){
			if(lwr_val >= max){
				lwr_val = max;
			}
			else{
				lwr_val = min;
			}
			
			lwr_delta *= -1;
		}
		
		t1 = NOW();
		msleep(40);
		delta = NSEC_TO_MSEC(NOW() - t1);
		
		if(delta > 45){
			printk("sleept for %lums\n", delta);
		}
	}
	do_exit();
}

int app_main(void *p)
{
    app_thread = create_thread("Headlight VM", run_client, NULL);
    //~ tick_thread = create_thread("LWR", lwr_thread, NULL);
         
    return 0;
}


void resume_eval_vm(int rc){
	
}

void suspend_eval_vm(void) {
	
}

void suspend_headlight_vm(void)
{
	//~ printk("suspend_headlight_vm\n");
}

void resume_headlight_vm(int rc)
{
	
	/* If we resume on a a new host the timing  / timestamp counter
	 * is usually not consistent with the old host.
	 * So if a thread is currently sleeping, it's thread->wakeup_time has been 
	 * set to a value that is not correct anymore, which causes the thread to not 
	 * wake up at all or only with a very large delay.
	 * To prevent this, we wake up the thread immediately, which might be 
	 * too soon, but that is better than not waking up at all and does not
	 * matter in this specific application anyway.
	 * 
	 * An alternative would be to save the delta that we still have to sleep 
	 * (thread->wakeup_time - NOW()) when suspending and set this as the new wakup time
	 * when we resume on the new host (wakup_time = NOW() + delta).
	 * In that case however, the thread will sleep for too long, 
	 * because the failover time was not accounted for and we have no way of knowing that 
	 * time without an external time reference.
	 */
	if(!rc){
		wake(app_thread);
		wake(tick_thread);
		
		// For evaluation only:
		// change 
	}
}

void request_checkpoint(void)
{
	if(!checkpoint_requested){
		xenbus_printf(XBT_NIL, "data", "ha","%d",1);
		checkpoint_requested = 1;	
	}
}

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

#define MSG_PERIOD_MS	10

unsigned long start_ts, end_ts;

struct vcanfront_dev 	*can_dev;
int16_t lwr_val = 0x07FF, glw_val = 0, afs_val = 0x07FF;
struct can_frame lwr_frame, glw_frame, afs_frame;

struct thread *app_thread, *tick_thread;


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

void run_client(void *p)
{
	int remaining_sleep = MSG_PERIOD_MS;
	
		
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
		
		

	init_afs();
	init_lwr();

	start_ts = NOW();
	while(1){
		
		// LWR is for up and down
		lwr_frame.data[0] = lwr_val;
		lwr_frame.data[1] = (lwr_val >> 8);
		
		lwr_frame.data[3] = lwr_frame.data[0];
		lwr_frame.data[4] = lwr_frame.data[1];
		
		vcanfront_send(can_dev, &lwr_frame);
		
		
		// AFS is for left and right
		
		//~ msleep(4);
		//~ afs_frame.data[0] = afs_val;
		//~ afs_frame.data[1] = (afs_val >> 8);
		//~ afs_frame.data[3] = afs_frame.data[0];
		//~ afs_frame.data[4] = afs_frame.data[1];
		//~ vcanfront_send(can_dev, &afs_frame);
		
		
		end_ts = NOW();
		// (requesting) checkpointing takes time, so we calculate the actual
		// time we have to sleep to achieve a period of MSG_PERIOD_MS
		remaining_sleep = MSG_PERIOD_MS - NSEC_TO_MSEC(end_ts-start_ts);
		
		/* the calculation above (end_ts-start_ts) gives wrong results 
		*  (e.g. very large numbers) sometimes for some reason , 
		*  so let's do a santiy check 
		*/
		if(remaining_sleep > MSG_PERIOD_MS || remaining_sleep < 0)
			remaining_sleep = MSG_PERIOD_MS;
			
		msleep(remaining_sleep);
		start_ts = NOW();
		request_checkpoint();

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
		
		msleep(40);
	}
	do_exit();
}

int app_main(void *p)
{
    app_thread = create_thread("Headlight VM", run_client, NULL);
    tick_thread = create_thread("LWR", lwr_thread, NULL);
         
    return 0;
}



void suspend_headlight_vm(void)
{
	//~ printk("suspend\n");
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
	}
}

void request_checkpoint(void)
{
	xenbus_printf(XBT_NIL, "data", "ha","%d",1);
}

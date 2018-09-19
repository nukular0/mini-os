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
#include <ma_eval.h>

#ifdef CONFIG_NETFRONT
#include <xen/io/netif.h>
#include <lwip/api.h>
#include <mini-os/netfront.h>
#endif

#define TX_ID	0x123
#define RSP_ID	0x194

#define REPS 2000

#define US_TO_COUNTER(x) (x/(5.9733/1000))

#define VM1_TIME_US	5000
#define VM2_TIME_US	9000
#define VM3_TIME_US	1

#define nEVENT

#ifdef EVAL
uint64_t req_chkpt_time[NUM_SAMPLES];
uint64_t pkt_send_time[NUM_SAMPLES];
uint32_t req_chkpt_sample_counter = 0;

int host = 0;

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

struct semaphore		work_available_sema;

void request_checkpoint(void);

struct vm_work_item {
	MINIOS_TAILQ_ENTRY(struct vm_work_item) tailq;
	struct can_frame				cf;
	uint64_t t_rx;
	int num;
};
MINIOS_TAILQ_HEAD(vm_work_q, struct vm_work_item);

struct vm_work_q work_queue;

// Leave these here!! They are required by mini-os
unsigned long t0, start_ts, end_ts, start_ts2, end_ts2;
long diff;
int running = 1;

uint64_t rtt[REPS];
uint32_t rep = 0;

struct vcanfront_dev *can_dev;

struct can_frame tx_frame, failover_frame;
canid_t	my_can_id = 0x123;
volatile uint32_t calc_time = US_TO_COUNTER(9000);

int run_eval = 0;

struct thread *m_thread;


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
	unsigned long flags;
	struct vm_work_item *item;

	if(cf->can_id == 0xEE){
		printk("Eval START\n");
		req_chkpt_sample_counter = 0;
		run_eval = 1;
		return;
	}
	
	
	if(cf->can_id != my_can_id && cf->can_id != 0xFF)
		return;
	
	item = (struct vm_work_item *)malloc(sizeof(struct vm_work_item));
	
	if(!item)
		return;
	
	item->t_rx = get_tsc();

	memcpy(&item->cf, cf, sizeof(*cf));
	item->num = rep++;
	//~ printk("rx msg %d at %lu\n", item->num, item->t_rx);
	local_irq_save(flags);
	MINIOS_TAILQ_INSERT_TAIL(&work_queue, item, tailq);
	local_irq_restore(flags);
	
	up(&work_available_sema);
}



void handle_work_item(struct vm_work_item *item)
{
	uint32_t counter = 0, time;

#ifdef EVENT
	uint64_t t1, checkpointing_time;
#else
	uint64_t checkpointing_time = 0;
#endif

#ifdef EVAL
	uint64_t t_pkt_send;
#endif	

	if(item->cf.can_id == 0xFF){
		printk("Eval STOP\n");
		run_eval = 0;
		dump_eval_data();
		return;
	}
	
	while(counter++ < calc_time){}
	
	end_ts = get_tsc();
	// factor 1.089 to go from cycles to ns
	time = (uint32_t)(tscs_to_ns(item->t_rx, end_ts, 1.089));
	
	//~ // host id is encoded in time variable
	time |= (host << 31);
	
	// copy time we spend processing this message (in ns) to outgoing message
	memcpy(&item->cf.data[4], &time, 4);
	//~ print_time_diff("sending response after ",item->t_rx, end_ts, 1.089);
	//start_ts = end_ts = 0;
	//~ print_can_frame("TX: ", &rsp_frame);
#ifdef EVAL
	t_pkt_send = NOW();
	if(t_resume_app){
		print_time_diff("sending packet ms after resume app: ", t_resume_app, t_pkt_send, 1);
		t_resume_app = 0;
	}	
#endif
	vcanfront_send(can_dev, &item->cf);
	
#ifdef EVENT
	//~ msleep(1);
	t1 = NOW();
	request_checkpoint();
	
	checkpointing_time = NOW() - t1;
	
#endif

#ifdef EVAL
		if(run_eval){
			if(req_chkpt_sample_counter < NUM_SAMPLES){
				pkt_send_time[req_chkpt_sample_counter] = t_pkt_send;
				req_chkpt_time[req_chkpt_sample_counter++] = checkpointing_time;
			}
			if(req_chkpt_sample_counter >= NUM_SAMPLES){
				dump_eval_data();
			}
		}
#endif		

//~ #ifdef EVENT
	//~ if(checkpointing_time > MILLISECS(2)){
		//~ printk("STOP! checkpointing took %llums\n", NSEC_TO_MSEC(checkpointing_time));
		
//~ #ifdef EVAL
		//~ dump_eval_data();
//~ #endif
		//~ msleep(100);
		//~ do_exit();
	//~ }
//~ #endif
	//~ printk("packet send\n");
}

void work(void)
{
	unsigned long flags;
	struct vm_work_item *item;
		
	while (!MINIOS_TAILQ_EMPTY(&work_queue)) {
		local_irq_save(flags);
		item = MINIOS_TAILQ_FIRST(&work_queue);
		MINIOS_TAILQ_REMOVE(&work_queue, item, tailq);
		local_irq_restore(flags);	

		handle_work_item(item);
		free(item);
    }

}


void run_client(void *p)
{
	int ret = 0;
	char *buf;
	int time;

    tprintk("CAN RTT bench app\n");
	init_SEMAPHORE(&work_available_sema, 0);
	MINIOS_TAILQ_INIT(&work_queue);
#ifdef CONFIG_NETFRONT
	start_networking();
#endif
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

	failover_frame.can_id = 0x100;
	failover_frame.can_dlc = 1;

	if(!strcmp("canvm1", buf)){
		my_can_id = 0x123;
		time = VM1_TIME_US;
		calc_time = US_TO_COUNTER(VM1_TIME_US);
	}
	else if(!strcmp("canvm2", buf)){
		my_can_id = 0x223;
		time = VM2_TIME_US;
		calc_time = US_TO_COUNTER(VM2_TIME_US);
	}
	else if(!strcmp("canvm3", buf)){
		my_can_id = 0x123;
		time = VM3_TIME_US;
		calc_time = US_TO_COUNTER(VM3_TIME_US);
	}
	
	
	tprintk("My WCET is %dus\n", time);
	tprintk("Will respond to CAN_ID 0x%X\n", my_can_id);
	
	
	free(buf);
		
	request_checkpoint();	
	
	while(1){
		down(&work_available_sema);
		work();
	}
		    
    shutdown_vcanfront(can_dev);
    do_exit();
}

#ifdef CONFIG_XENBUS
void app_shutdown(unsigned reason)
{
#ifdef CONFIG_NETFRONT
	stop_networking();
#endif
	shutdown_vcanfront(can_dev);
    struct sched_shutdown sched_shutdown = { .reason = reason };
    printk("CAN RTT benchmark is shutting down: %d\n", reason);
    HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}
#endif

//~ void loop(void* p){
	//~ while(1){
		//~ printk("tick\n");
		//~ msleep(1000);
	//~ }
//~ }

int app_main(void *p)
{
    m_thread = create_thread("CAN_RTT", run_client, NULL);
    //~ create_thread("Loop", loop, NULL);
     
    return 0;
}

void request_checkpoint(void)
{
	if(!checkpoint_requested){
		xenbus_printf(XBT_NIL, "data", "ha","%d",1);
		checkpoint_requested = 1;	
	}
}

#ifdef EVAL
void resume_eval_vm(int rc)
{
	if(!rc){
		host = 1;
		vcanfront_send(can_dev, &failover_frame);
		t_resume_app = NOW();
		//~ printk("Now on new host!\n");
	}
}

void suspend_eval_vm(void)
{
	
}
#endif

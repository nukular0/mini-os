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
#include <mini-os/vcanfront.h>
#include <mini-os/can.h>
#include <mini-os/can_error.h>


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

// Leave these here!! They are required by mini-os
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long t0, start_ts, end_ts, start_ts2, end_ts2;
long diff;
int running = 1;

struct vcanfront_dev 	*can_dev;
struct semaphore		work_available_sema;


struct sensorvm_work_item {
	MINIOS_TAILQ_ENTRY(struct sensorvm_work_item) tailq;
	struct can_frame				cf;
};
MINIOS_TAILQ_HEAD(sensorvm_work_q, struct sensorvm_work_item);

struct sensorvm_work_q work_queue;


void print_can_error(struct can_frame *cf)
{
	if(cf->can_id & CAN_ERR_TX_TIMEOUT)
		printk("TX timeout (by netdevice driver)");
	else if(cf->can_id & CAN_ERR_LOSTARB)
		printk("lost arbitration");
	else if(cf->can_id & CAN_ERR_CRTL)
		printk("controller problems");
	else if(cf->can_id & CAN_ERR_PROT)
		printk("protocol violations");
	else if(cf->can_id & CAN_ERR_TRX)
		printk("transceiver status");
	else if(cf->can_id & CAN_ERR_ACK)
		printk("received no ACK on transmission");
	else if(cf->can_id & CAN_ERR_BUSOFF)
		printk("bus off");
	else if(cf->can_id & CAN_ERR_BUSERROR)
		printk("bus error (may flood!)");
	else if(cf->can_id & CAN_ERR_RESTARTED)
		printk("controller restarted");
}

void print_can_frame(char* prefix, struct can_frame *cf)
{
	int i = 0;
	if(can_frame_error(cf)){
		tprintk("%sError Frame: ", prefix);
		print_can_error(cf);
		printk("\n");
		return;			
	}
	else if(can_frame_rtr(cf)){
		can_frame_remove_flags(cf);
		tprintk("%sRTR frame for id %X\n", prefix, cf->can_id);
		return;
	}
	
	can_frame_remove_flags(cf);
	 
	tprintk("%s%8X [%d]", prefix, cf->can_id, cf->can_dlc);
	for(; i < cf->can_dlc; i++){
		printk(" %02X ", cf->data[i]);			
	}
	printk("\n");	
}


/*All Data Values Signed 16 bit sent LSB first (little endian)
* Data Packets
* Identifier 	Data1 			Data2 				Data3 				Data4
* 0x2000 		RPM 			TPS % 				Water Temp C 		Air Temp C
* 0x2001 		MAP Kpa 		Lambda x 1000 		KPH x 10			Oil P Kpa
* 0x2002 		Fuel P Kpa 		Oil Temp C 			Volts x 10 			Fuel Con. L/Hr x 10
* 0x2003 		Gear 			Advance Deg x 10	Injection ms x 100 	Fuel Con L/100Km x 10
* 
* From V62.01 the items below were added.
* 0x2004 		Ana1 mV 		Ana2 mV 			Ana3 mV 			Cam Advance x 10
* 0x2005 		Cam Targ x 10 	Cam PWM x 10 		Crank Errors 		Cam Errors
* 
* From V79.02 the items below were added.
* 0x2006 		Cam2 Adv x 10 	Cam2 Targ x 10 		Cam2 PWM x 10 		External 5v
* 
* From V79.04 the items below were added.
* 0x2007 		Inj Duty Cycle % Lambda PID Target 	Lambda PID Adj		---
*/
void can_rx_handler(struct can_frame *cf)
{
	struct sensorvm_work_item *item = (struct sensorvm_work_item *)malloc(sizeof(struct sensorvm_work_item));
	
	if(!item)
		return;
	
	memcpy(&item->cf, cf, sizeof(*cf));
	
	MINIOS_TAILQ_INSERT_TAIL(&work_queue, item, tailq);
	
	up(&work_available_sema);
}

void handle_can_frame(struct can_frame *cf)
{
	xenbus_transaction_t 	xbt;
	char* 					err;
	char					path[512];
	int						retry;
	unsigned long 			start_ts;
	
	if(!can_frame_valid_data(cf)){
		print_can_frame("RX : ", cf);
		return;
	}
	
	//~ print_can_frame("RX : ", cf);
	can_frame_remove_flags(cf);

again:	
	if(cf->can_id >= 0x2000 && cf->can_id <= 0x2007){
		if((err = xenbus_transaction_start(&xbt))) {
		  tprintk("Unable to start xenbus transaction, error was %s\n", err);
		  free(err);
		  return;
	   }
	   
		snprintf(path, 512, "%s/ecu", XENSTORE_SENSOR_PATH);
		switch(cf->can_id){
			case 0x2000:
				err = xenbus_printf(xbt, path, "rpm", "%d", TO_INT16(cf->data[0],cf->data[1]));
				if(err){
					tprintk("Could not write to xenstore: %s\n", err);
					free(err);
				}
				xenbus_printf(xbt, path, "tps_percent", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "water_temp_c", "%d", TO_INT16(cf->data[4],cf->data[5]));
				xenbus_printf(xbt, path, "air_temp_c", "%d", TO_INT16(cf->data[6],cf->data[7]));
				break;
			case 0x2001:
				xenbus_printf(xbt, path, "map_kpa", "%d", TO_INT16(cf->data[0],cf->data[1]));
				xenbus_printf(xbt, path, "lambda_x1000", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "kph_x10", "%d", TO_INT16(cf->data[4],cf->data[5]));
				xenbus_printf(xbt, path, "oil_p_kpa", "%d", TO_INT16(cf->data[6],cf->data[7]));
				break;
			case 0x2002:
				xenbus_printf(xbt, path, "fuel_p_kpa", "%d", TO_INT16(cf->data[0],cf->data[1]));
				xenbus_printf(xbt, path, "oil_temp_c", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "volts_x10", "%d", TO_INT16(cf->data[4],cf->data[5]));
				xenbus_printf(xbt, path, "fuel_con_l_hr_x10", "%d", TO_INT16(cf->data[6],cf->data[7]));
				break;
			case 0x2003:
				xenbus_printf(xbt, path, "gear", "%d", TO_INT16(cf->data[0],cf->data[1]));
				xenbus_printf(xbt, path, "advance_deg_x100", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "injection_ms_x100", "%d", TO_INT16(cf->data[4],cf->data[5]));
				xenbus_printf(xbt, path, "fuel_con_l_100km_x10", "%d", TO_INT16(cf->data[6],cf->data[7]));
				break;
			case 0x2004:
				xenbus_printf(xbt, path, "ana1", "%d", TO_INT16(cf->data[0],cf->data[1]));
				xenbus_printf(xbt, path, "ana2", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "ana3", "%d", TO_INT16(cf->data[4],cf->data[5]));
				xenbus_printf(xbt, path, "cam_advance_x10", "%d", TO_INT16(cf->data[6],cf->data[7]));
				break;
			case 0x2005:
				xenbus_printf(xbt, path, "cam_targ_x10", "%d", TO_INT16(cf->data[0],cf->data[1]));
				xenbus_printf(xbt, path, "cam_pwm_x10", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "crank_errors", "%d", TO_INT16(cf->data[4],cf->data[5]));
				xenbus_printf(xbt, path, "cam_errors", "%d", TO_INT16(cf->data[6],cf->data[7]));
				break;
			case 0x2006:
				xenbus_printf(xbt, path, "cam2_adv_x10", "%d", TO_INT16(cf->data[0],cf->data[1]));
				xenbus_printf(xbt, path, "cam2_targ_x10", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "cam2_pwm_x10", "%d", TO_INT16(cf->data[4],cf->data[5]));
				xenbus_printf(xbt, path, "external_5v", "%d", TO_INT16(cf->data[6],cf->data[7]));
				break;
			case 0x2007:
				xenbus_printf(xbt, path, "inj_duty_cycle", "%d", TO_INT16(cf->data[0],cf->data[1]));
				xenbus_printf(xbt, path, "lambda_pid_target", "%d", TO_INT16(cf->data[2],cf->data[3]));
				xenbus_printf(xbt, path, "lambda_pid_adj", "%d", TO_INT16(cf->data[4],cf->data[5]));
				break;
		
		}
		
		if((err = xenbus_transaction_end(xbt, 0, &retry))) {
			tprintk("Unable to complete xenbus transaction, error was %s\n", err);
			free(err);
			return;
		}
	
		if(retry) {
		  goto again;
		}
	}
	else if(cf->can_id == 0x666){
		CYCLES_NOW_START(cycles_low, cycles_high);
		start_ts = ( ((unsigned long)cycles_high << 32) | cycles_low );
		if((err = xenbus_transaction_start(&xbt))) {
		  tprintk("Unable to start xenbus transaction, error was %s\n", err);
		  free(err);
		  return;
	   }
	   
		snprintf(path, 512, "%s/timing", XENSTORE_SENSOR_PATH);
		
		err = xenbus_printf(xbt, path, "start", "%lu", start_ts);
		xenbus_transaction_end(xbt, 0, &retry);
		//~ if(err){
			//~ tprintk("Could not write to xenstore: %s\n", err);
			//~ free(err);
		//~ }
		//~ else{
			//~ tprintk("written to %s: %lu\n", path, start_ts);
		//~ }
		
	}
	else{
		print_can_frame("Not a sensor message: ", cf);
	}
}

void work(void)
{
	unsigned long flags;
	struct sensorvm_work_item *item;
		
	while (!MINIOS_TAILQ_EMPTY(&work_queue)) {
		local_irq_save(flags);
		item = MINIOS_TAILQ_FIRST(&work_queue);
		MINIOS_TAILQ_REMOVE(&work_queue, item, tailq);
		local_irq_restore(flags);	

		handle_can_frame(&item->cf);

		free(item);
    }

}

void run_client(void *p)
{
	int ret;
	
    tprintk("Sensor VM started!\n");
        
	can_dev = init_vcanfront("device/vcan/0");

	if(!can_dev)
		return;
		
	ret = vcanfront_register_rx_handler(can_dev, can_rx_handler);
	
	if(ret){
		tprintk("registering rx_handler failed (%d)\n", ret);
		return;
	}
	
	init_SEMAPHORE(&work_available_sema, 0);
	MINIOS_TAILQ_INIT(&work_queue);
	
	
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



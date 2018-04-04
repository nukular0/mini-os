#include <mini-os/os.h>
#include <mini-os/xenbus.h>
#include <mini-os/xmalloc.h>
#include <mini-os/events.h>
#include <mini-os/wait.h>
#include <mini-os/gnttab.h>
#include <xen/io/xenbus.h>
#include <mini-os/lib.h>
#include <fcntl.h>
#include <mini-os/time.h>
#include <mini-os/errno.h>
#include <vgpiofront.h>

#define VGPIOFRONT_PRINT_DEBUG
#ifdef VGPIOFRONT_PRINT_DEBUG
#define VGPIO_DEBUG(fmt,...) printk("vgpiofront:Debug("__FILE__":%d) " fmt, __LINE__, ##__VA_ARGS__)
#define VGPIO_DEBUG_MORE(fmt,...) printk(fmt, ##__VA_ARGS__)
#else
#define VGPIO_DEBUG(fmt,...)
#endif
#define VGPIOFRONT_ERR(fmt,...) printk("vgpiofront:Error " fmt, ##__VA_ARGS__)
#define VGPIOFRONT_LOG(fmt,...) printk("vgpiofront:Info " fmt, ##__VA_ARGS__)

s_time_t t1, t2, us, ns;

static void free_vgpiofront(struct vgpiofront_dev *dev)
{
    struct _pin_irq *elm, *telm;
    mask_evtchn(dev->comm_evtchn);

	if(dev->bepath)
		free(dev->bepath);

    gnttab_end_access(dev->ring_ref);
    free_page(dev->ring.sring);

    unbind_evtchn(dev->comm_evtchn);

	// free irq_list
	// same as LIST_FOREACH_SAFE(elm, &dev->irq_list, list, telm), but that macro does not exist for some reason
	for ((elm) = LIST_FIRST((&dev->irq_list)); (elm) && ((telm) = LIST_NEXT((elm), list), 1); (elm) = (telm)){
		mask_evtchn(elm->port);
		unbind_evtchn(elm->port);
		LIST_REMOVE(elm, list);
		free(elm);	
		
	}

    free(dev->nodename);
    free(dev);
}

int vgpiofront_send_request(struct vgpiofront_dev* dev, vgpio_request_t req){
	RING_IDX i;
	vgpio_request_t *_req;
	int notify;
	//~ int err;
	
	//~ VGPIOFRONT_LOG("sending...");
	if(dev->state == XenbusStateConnected){
		i = dev->ring.req_prod_pvt;
		_req = RING_GET_REQUEST(&dev->ring, i);
		memcpy(_req, &req, sizeof(req));
		dev->ring.req_prod_pvt = i + 1;

		wmb();
		RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&dev->ring, notify);
		if(notify) 
		{
			VGPIO_DEBUG("notifying %d\n", dev->comm_evtchn);
			notify_remote_via_evtchn(dev->comm_evtchn);    
			VGPIO_DEBUG("notified\n");
		}
		down(&dev->sem);
	}
	else{
		VGPIO_DEBUG("error: not connected");
		return -1;
	}
	int _ret = dev->last_response.ret;
	dev->last_response.ret = INVALID_RESPONSE;
	return _ret;
}

void vgpiofront_hw_irq_handler(evtchn_port_t port, struct pt_regs *regs, void *data)
{
	// data is a pointer to the handler that was registered for this event/irq
	void (*handler)(void) = (void(*)(void))data;
	handler();
}
void vgpiofront_handler(evtchn_port_t port, struct pt_regs *regs, void *data)
{
	RING_IDX rp, cons;
	vgpio_response_t *rsp;
	int nr_consumed, more;
	struct vgpiofront_dev *dev = (struct vgpiofront_dev*) data;
	//~ t2 = NOW();
	//~ us = (t2-t1)/1000;
	//~ ns = (t2-t1) - (us*1000);
	//~ tprintk("handler called after: %lu,%luus\n", us,ns);

moretodo:
	rp = dev->ring.sring->rsp_prod;
    rmb(); /* Ensure we see queued responses up to 'rp'. */
    cons = dev->ring.rsp_cons;
   
	while ((cons != rp))
    {
		rsp = RING_GET_RESPONSE(&dev->ring, cons);
		nr_consumed++;
		dev->last_response = *rsp;
		dev->ring.rsp_cons = ++cons;   
		up(&dev->sem);   
        if (dev->ring.rsp_cons != cons)
            /* We reentered, we must not continue here */
            break;
			
	}
	
	RING_FINAL_CHECK_FOR_RESPONSES(&dev->ring, more);
    if (more) goto moretodo;
	
}

static int publish_xenbus(struct vgpiofront_dev* dev) {
   xenbus_transaction_t xbt;
   int retry;
   char* err;
   /* Write the grant reference and event channel to xenstore */
again:
   if((err = xenbus_transaction_start(&xbt))) {
      VGPIOFRONT_ERR("Unable to start xenbus transaction, error was %s\n", err);
      free(err);
      return -1;
   }

   if((err = xenbus_printf(xbt, dev->nodename, "ring-ref", "%u", (unsigned int) dev->ring_ref))) {
      VGPIOFRONT_ERR("Unable to write %s/ring-ref, error was %s\n", dev->nodename, err);
      free(err);
      goto abort_transaction;
   }

   if((err = xenbus_printf(xbt, dev->nodename, "event-channel", "%u", (unsigned int) dev->comm_evtchn))) {
      VGPIOFRONT_ERR("Unable to write %s/event-channel, error was %s\n", dev->nodename, err);
      free(err);
      goto abort_transaction;
   }

   if((err = xenbus_transaction_end(xbt, 0, &retry))) {
      VGPIOFRONT_ERR("Unable to complete xenbus transaction, error was %s\n", err);
      free(err);
      return -1;
   }
   if(retry) {
      goto again;
   }

   return 0;
abort_transaction:
   if((err = xenbus_transaction_end(xbt, 1, &retry))) {
      free(err);
   }
   return -1;
}

static int wait_for_backend_connect(xenbus_event_queue* events, char* path)
{
   int state;

   VGPIOFRONT_LOG("Waiting for backend connection on path ");
   printk("%s\n", path);
   /* Wait for the backend to connect */
   while(1) {
      state = xenbus_read_integer(path);
      if ( state < 0)
	 state = XenbusStateUnknown;
      switch(state) {
	 /* Bad states, we quit with error */
	 case XenbusStateUnknown:
	 case XenbusStateClosing:
	 case XenbusStateClosed:
	    VGPIOFRONT_ERR("Unable to connect to backend\n");
	    return -1;
	 /* If backend is connected then break out of loop */
	 case XenbusStateConnected:
	    VGPIOFRONT_LOG("Backend Connected\n");
	    return 0;
	 default:
	    xenbus_wait_for_watch(events);
      }
   }

}

static int wait_for_backend_closed(xenbus_event_queue* events, char* path)
{
   int state;

   VGPIOFRONT_LOG("Waiting for backend to close..\n");
   while(1) {
      state = xenbus_read_integer(path);
      if ( state < 0)
	 state = XenbusStateUnknown;
      switch(state) {
	 case XenbusStateUnknown:
	    VGPIOFRONT_ERR("Backend Unknown state, forcing shutdown\n");
	    return -1;
	 case XenbusStateClosed:
	    VGPIOFRONT_LOG("Backend Closed\n");
	    return 0;
	 case XenbusStateInitWait:
	    VGPIOFRONT_LOG("Backend Closed (waiting for reconnect)\n");
	    return 0;
	 default:
	    xenbus_wait_for_watch(events);
      }
   }

}

static int wait_for_backend_state_changed(struct vgpiofront_dev* dev, XenbusState state) {
   char* err;
   int ret = 0;
   xenbus_event_queue events = NULL;
   char path[512];

   snprintf(path, 512, "%s/state", dev->bepath);
   /*Setup the watch to wait for the backend */
   if((err = xenbus_watch_path_token(XBT_NIL, path, path, &events))) {
      VGPIOFRONT_ERR("Could not set a watch on %s, error was %s\n", path, err);
      free(err);
      return -1;
   }

   /* Do the actual wait loop now */
   switch(state) {
      case XenbusStateConnected:
	 ret = wait_for_backend_connect(&events, path);
	 break;
      case XenbusStateClosed:
	 ret = wait_for_backend_closed(&events, path);
	 break;
      default:
         VGPIOFRONT_ERR("Bad wait state %d, ignoring\n", state);
   }

   if((err = xenbus_unwatch_path_token(XBT_NIL, path, path))) {
      VGPIOFRONT_ERR("Unable to unwatch %s, error was %s, ignoring..\n", path, err);
      free(err);
   }
   return ret;
}

static int vgpiofront_connect(struct vgpiofront_dev* dev)
{
	
	char* err;
	struct vgpio_sring *sring;
   
	/* Create shared page/ring */
	sring = (struct vgpio_sring *)alloc_page();
	if(sring == NULL) {
	  VGPIOFRONT_ERR("Unable to allocate page for shared memory\n");
	  goto error;	
	}
	memset(sring, 0, PAGE_SIZE);
	dev->ring_ref = gnttab_grant_access(dev->bedomid, virt_to_mfn(sring), 0);
	//~ VGPIO_DEBUG("grant ref is %lu\n", (unsigned long) dev->ring_ref);

	/* Initialize shared ring in shared page */
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&dev->ring, sring, PAGE_SIZE);
	
	/* Create event channel for communication with backend */
	if(evtchn_alloc_unbound(dev->bedomid, vgpiofront_handler, dev, &dev->comm_evtchn)) {
	  VGPIOFRONT_ERR("Unable to allocate comm_event channel\n");
	  goto error_postmap;
	}
	unmask_evtchn(dev->comm_evtchn);
	//~ VGPIO_DEBUG("comm_event channel is %lu\n", (unsigned long) dev->comm_evtchn);

	

	/* Write the entries to xenstore */
	if(publish_xenbus(dev)) {
	  goto error_postevtchn;
	}

	/* Change state to connected */
	dev->state = XenbusStateConnected;

	/* Tell the backend that we are ready */
	if((err = xenbus_printf(XBT_NIL, dev->nodename, "state", "%u", dev->state))) {
	  VGPIOFRONT_ERR("Unable to write to xenstore %s/state, value=%u", dev->nodename, XenbusStateConnected);
	  free(err);
	  goto error;
	}

	return 0;
error_postevtchn:
      mask_evtchn(dev->comm_evtchn);
      unbind_evtchn(dev->comm_evtchn);
error_postmap:
      gnttab_end_access(dev->ring_ref);
      free_page(sring);
error:
   return -1;
}

/**
 * Call this from the application / thread.
 *  - Reads id of backend domain from xenstore
 *  - Reads xenstore path of backend from xenstore
 *  - connects frontend: calls vgpiofront_connect()
 *  - waits for backend to connect
 *  --> initialization of frontend done
 */
struct vgpiofront_dev* init_vgpiofront(const char* _nodename)
{
	struct vgpiofront_dev* dev;
	const char* nodename;
	char path[512];
	char* value;
	char* err;
	unsigned long long ival;

	printk("============= Init VGPIO Front ================\n");

	dev = malloc(sizeof(*dev));
	memset(dev, 0, sizeof(*dev));
	
	dev->last_response.ret = INVALID_RESPONSE;


	/* Init semaphore */
	init_SEMAPHORE(&dev->sem, 0);
	/* Set node name */
	nodename = _nodename ? _nodename : "device/vgpio/0";
	dev->nodename = strdup(nodename);
  
	/* Init irq List */
	LIST_INIT(&dev->irq_list);

	/* Get backend domid */
	snprintf(path, 512, "%s/backend-id", dev->nodename);
	if((err = xenbus_read(XBT_NIL, path, &value))) {
	  VGPIOFRONT_ERR("Unable to read %s during vgpiofront initialization! error = %s\n", path, err);
	  free(err);
	  goto error;
	}
	if(sscanf(value, "%llu", &ival) != 1) {
	  VGPIOFRONT_ERR("%s has non-integer value (%s)\n", path, value);
	  free(value);
	  goto error;
	}
	free(value);
	dev->bedomid = ival;
	VGPIOFRONT_LOG("backend dom-id is %llu\n", ival);

	/* Get backend xenstore path */
	snprintf(path, 512, "%s/backend", dev->nodename);
	if((err = xenbus_read(XBT_NIL, path, &dev->bepath))) {
	  VGPIOFRONT_ERR("Unable to read %s during vgpiofront initialization! error = %s\n", path, err);
	  free(err);
	  goto error;
	}
	VGPIOFRONT_LOG("backend path is %s\n", dev->bepath);



	/* Create and publish grant reference and event channel */
	if (vgpiofront_connect(dev)) {
	  goto error;
	}

	/* Wait for backend to connect */
	if( wait_for_backend_state_changed(dev, XenbusStateConnected)) {
	  goto error;
	}

	return dev;

error:
   shutdown_vgpiofront(dev);
   return NULL;
}
void shutdown_vgpiofront(struct vgpiofront_dev* dev)
{
   char* err;
   char path[512];
   if(dev == NULL) {
      return;
   }
   //~ VGPIOFRONT_LOG("Shutting down vgpiofront\n");
   /* disconnect */
   if(dev->state == XenbusStateConnected) {
      /* Tell backend we are closing */
      dev->state = XenbusStateClosing;
      if((err = xenbus_printf(XBT_NIL, dev->nodename, "state", "%u", (unsigned int) dev->state))) {
	 VGPIOFRONT_ERR("Unable to write to %s, error was %s", dev->nodename, err);
	 free(err);
      }

      /* Clean up xenstore entries */
      snprintf(path, 512, "%s/event-channel", dev->nodename);
      if((err = xenbus_rm(XBT_NIL, path))) {
	 free(err);
      }
      snprintf(path, 512, "%s/ring-ref", dev->nodename);
      if((err = xenbus_rm(XBT_NIL, path))) {
	 free(err);
      }

      /* Tell backend we are closed */
      dev->state = XenbusStateClosed;
      if((err = xenbus_printf(XBT_NIL, dev->nodename, "state", "%u", (unsigned int) dev->state))) {
	 VGPIOFRONT_ERR("Unable to write to %s, error was %s", dev->nodename, err);
	 free(err);
      }

      /* Wait for the backend to close and unmap shared pages, ignore any errors */
      wait_for_backend_state_changed(dev, XenbusStateClosed);

      /* Prepare for a later reopen (possibly by a kexec'd kernel) */
      dev->state = XenbusStateInitialising;
      if((err = xenbus_printf(XBT_NIL, dev->nodename, "state", "%u", (unsigned int) dev->state))) {
		VGPIOFRONT_ERR("Unable to write to %s, error was %s", dev->nodename, err);
		free(err);
      }

     
   }

   free_vgpiofront(dev);
}

int gpio_request(struct vgpiofront_dev *dev, unsigned gpio, const char *label)
{
	vgpio_request_t req = {
		.cmd = CMD_GPIO_REQUEST,
		.pin = gpio,
	};
	return vgpiofront_send_request(dev, req);
}
void gpio_free(struct vgpiofront_dev *dev, unsigned gpio)
{
	vgpio_request_t req = {
		.cmd = CMD_GPIO_FREE,
		.pin = gpio,
	};
	vgpiofront_send_request(dev, req);
}

int gpio_direction_input(struct vgpiofront_dev *dev, unsigned gpio)
{
	vgpio_request_t req = {
		.cmd = CMD_GPIO_DIRECTION_INPUT,
		.pin = gpio,
	};
	return vgpiofront_send_request(dev, req);
}

int gpio_direction_output(struct vgpiofront_dev *dev, unsigned gpio, int value)
{
	vgpio_request_t req = {
		.cmd = CMD_GPIO_DIRECTION_OUTPUT,
		.pin = gpio,
		.val = value,
	};
	return vgpiofront_send_request(dev, req);
}

int gpio_set_debounce(struct vgpiofront_dev *dev, unsigned gpio, unsigned debounce)
{
	vgpio_request_t req = {
		.cmd = CMD_GPIO_SET_DEBOUNCE,
		.pin = gpio,
		.val = debounce,
	};
	return vgpiofront_send_request(dev, req);
}

int gpio_get_value(struct vgpiofront_dev *dev, unsigned gpio)
{
	vgpio_request_t req = {
		.cmd = CMD_GPIO_GET_VALUE,
		.pin = gpio,
	};
	return vgpiofront_send_request(dev, req);
}

void gpio_set_value(struct vgpiofront_dev *dev, unsigned gpio, int value)
{
	vgpio_request_t req = {
		.cmd = CMD_GPIO_SET_VALUE,
		.pin = gpio,
		.val = value,
	};
	vgpiofront_send_request(dev, req);
}

int gpio_request_irq(struct vgpiofront_dev *dev, unsigned gpio, void (*handler))
{
	evtchn_port_t _irq_evtchn;
	int err;
	
	if(!dev){
		VGPIOFRONT_ERR("gpio_request_irq: dev must not be NULL\n");
		return -1;
	}
	
	if(!handler){
		VGPIOFRONT_ERR("gpio_request_irq: handler must not be NULL\n");
		return -1;
	}
	
	/* Create event channel for gpio interrupts */
	if(evtchn_alloc_unbound(dev->bedomid, vgpiofront_hw_irq_handler, handler, &_irq_evtchn)) {
	  VGPIOFRONT_ERR("Unable to allocate irq_event channel\n");
	  return -1;
	}
	unmask_evtchn(_irq_evtchn);
	//~ VGPIO_DEBUG("irq_event channel is %lu\n", (unsigned long) _irq_evtchn);
	
	vgpio_request_t req = {
		.cmd = CMD_GPIO_REQUEST_IRQ,
		.pin = gpio,
		.val = (unsigned int)_irq_evtchn,
	};
	
	/* alloc memory for irq_list entry 
	 * do it before vgpiofront_send_request(), so that if it should fail,
	 * wo do not have to undo the irq_request in the backend */
	struct _pin_irq *pin_irq = (struct _pin_irq*)malloc(sizeof(struct _pin_irq));
	if(!pin_irq){
		VGPIO_DEBUG("gpio_request_irq: malloc() for pin_irq failed\n");
		unbind_evtchn(_irq_evtchn);
		return -ENOMEM;
	}
	
	/* Request IRQ for pin from backend */
	if((err = vgpiofront_send_request(dev, req))){
		VGPIO_DEBUG("gpio_request_irq: backend could not request IRQ, error: %d\n", err);
		unbind_evtchn(_irq_evtchn);
		return err;
	}
	
	/* Add irq to dev's irq_list */
	pin_irq->pin = gpio;
	pin_irq->handler = handler;
	pin_irq->port = _irq_evtchn;
	LIST_INSERT_HEAD(&dev->irq_list, pin_irq, list);
	
	return 0;
}

void gpio_free_irq(struct vgpiofront_dev *dev, unsigned gpio)
{
	struct _pin_irq *tmp;
	evtchn_port_t _port;
	
	if(!dev){
		VGPIOFRONT_ERR("gpio_request_irq: dev must not be NULL\n");
	}
	
	
	// get event channel port for gpio from dev's irq_list
	LIST_FOREACH(tmp, &dev->irq_list, list){
		if(tmp->pin == gpio){
			_port = tmp->port;
			break;
		}
	}
	
	// mask event channel: we do not want interrupts anymore
	mask_evtchn(_port);
	
	// Send free request with pin number to backend
	vgpio_request_t req = {
		.cmd = CMD_GPIO_FREE_IRQ,
		.pin = gpio,
		.val = 0,
	};
	vgpiofront_send_request(dev, req);
	
	// unbind event channel
	unbind_evtchn(_port);
	
	// remove irq from dev's irq_list and free memory
	LIST_REMOVE(tmp, list);
	free(tmp);
}

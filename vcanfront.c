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
#include <mini-os/vcanfront.h>

#define VCANFRONT_PRINT_DEBUG
#ifdef VCANFRONT_PRINT_DEBUG
#define VCAN_DEBUG(fmt,...) printk("vcanfront:Debug("__FILE__":%d) " fmt, __LINE__, ##__VA_ARGS__)
#define VCAN_DEBUG_MORE(fmt,...) printk(fmt, ##__VA_ARGS__)
#else
#define VCAN_DEBUG(fmt,...)
#endif
#define VCANFRONT_ERR(fmt,...) printk("vcanfront:Error " fmt, ##__VA_ARGS__)
#define VCANFRONT_LOG(fmt,...) printk("vcanfront:Info " fmt, ##__VA_ARGS__)

s_time_t t1, t2, us, ns;

struct vcanfront_dev* vcandev;

evtchn_port_t vcanfront_evtchn = -1;

static void free_vcanfront(struct vcanfront_dev *dev)
{
    mask_evtchn(dev->comm_evtchn);

	if(dev->bepath)
		free(dev->bepath);

    gnttab_end_access(dev->ring_ref);
    free_page(dev->ring.sring);

    unbind_evtchn(dev->comm_evtchn);


    free(dev->nodename);
    free(dev);
}

uint32_t req_counter = 0;

int vcanfront_send_request(struct vcanfront_dev *dev, vcan_request_t *req){
	RING_IDX i;
	vcan_request_t *_req;
	int notify;

	
	if(dev->state == XenbusStateConnected){
		i = dev->ring.req_prod_pvt;
		_req = RING_GET_REQUEST(&dev->ring, i);
		memcpy(_req, req, sizeof(vcan_request_t));
		dev->ring.req_prod_pvt = i + 1;

		wmb();
		RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&dev->ring, notify);
		//~ VCAN_DEBUG("req_counter: %d\n", req_counter++);
		if(notify) 
		{
			notify_remote_via_evtchn(dev->comm_evtchn);    
			//~ VCAN_DEBUG("notified (0x%X)\n", req->frame.can_id);
			
		}
		else{
			//~ VCAN_DEBUG("not notified (0x%X)\n", req->frame.can_id);
		}
	}
	else{
		VCAN_DEBUG("error: not connected");
		return -1;
	}

	return 0;
}


void vcanfront_handler(evtchn_port_t port, struct pt_regs *regs, void *data)
{
	RING_IDX rp, cons;
	vcan_response_t *rsp;
	int more;
	struct vcanfront_dev *dev = (struct vcanfront_dev*) data;
	
	//~ if(vcanfront_suspended)
		//~ return;

moretodo:
	rp = dev->ring.sring->rsp_prod;
    rmb(); /* Ensure we see queued responses up to 'rp'. */
    cons = dev->ring.rsp_cons;
   
	while ((cons != rp))
    {
		rsp = RING_GET_RESPONSE(&dev->ring, cons);
		
		if(dev->rx_handler)
			dev->rx_handler(&rsp->frame);
		
		dev->ring.rsp_cons = ++cons;   
        if (dev->ring.rsp_cons != cons)
            /* We reentered, we must not continue here */
            break;
			
	}
	
	RING_FINAL_CHECK_FOR_RESPONSES(&dev->ring, more);
    if (more) goto moretodo;
	
}

static int publish_xenbus(struct vcanfront_dev* dev) {
   xenbus_transaction_t xbt;
   int retry;
   char* err;
   /* Write the grant reference and event channel to xenstore */
again:
   if((err = xenbus_transaction_start(&xbt))) {
      VCANFRONT_ERR("Unable to start xenbus transaction, error was %s\n", err);
      free(err);
      return -1;
   }

   if((err = xenbus_printf(xbt, dev->nodename, "ring-ref", "%u", (unsigned int) dev->ring_ref))) {
      VCANFRONT_ERR("Unable to write %s/ring-ref, error was %s\n", dev->nodename, err);
      free(err);
      goto abort_transaction;
   }

   if((err = xenbus_printf(xbt, dev->nodename, "event-channel", "%u", (unsigned int) dev->comm_evtchn))) {
      VCANFRONT_ERR("Unable to write %s/event-channel, error was %s\n", dev->nodename, err);
      free(err);
      goto abort_transaction;
   }

   if((err = xenbus_transaction_end(xbt, 0, &retry))) {
      VCANFRONT_ERR("Unable to complete xenbus transaction, error was %s\n", err);
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

   VCANFRONT_LOG("Waiting for backend connection on path ");
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
	    VCANFRONT_ERR("Unable to connect to backend\n");
	    return -1;
	 /* If backend is connected then break out of loop */
	 case XenbusStateConnected:
	    VCANFRONT_LOG("Backend Connected\n");
	    return 0;
	 default:
	    xenbus_wait_for_watch(events);
      }
   }

}

static int wait_for_backend_closed(xenbus_event_queue* events, char* path)
{
   int state;

   VCANFRONT_LOG("Waiting for backend to close..\n");
   while(1) {
      state = xenbus_read_integer(path);
      if ( state < 0)
	 state = XenbusStateUnknown;
      switch(state) {
	 case XenbusStateUnknown:
	    VCANFRONT_ERR("Backend Unknown state, forcing shutdown\n");
	    return -1;
	 case XenbusStateClosed:
	    VCANFRONT_LOG("Backend Closed\n");
	    return 0;
	 case XenbusStateInitWait:
	    VCANFRONT_LOG("Backend Closed (waiting for reconnect)\n");
	    return 0;
	 default:
	    xenbus_wait_for_watch(events);
      }
   }

}

static int wait_for_backend_state_changed(struct vcanfront_dev* dev, XenbusState state) {
   char* err;
   int ret = 0;
   xenbus_event_queue events = NULL;
   char path[512];

   snprintf(path, 512, "%s/state", dev->bepath);
   /*Setup the watch to wait for the backend */
   if((err = xenbus_watch_path_token(XBT_NIL, path, path, &events))) {
      VCANFRONT_ERR("Could not set a watch on %s, error was %s\n", path, err);
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
         VCANFRONT_ERR("Bad wait state %d, ignoring\n", state);
   }

   if((err = xenbus_unwatch_path_token(XBT_NIL, path, path))) {
      VCANFRONT_ERR("Unable to unwatch %s, error was %s, ignoring..\n", path, err);
      free(err);
   }
   return ret;
}

static int vcanfront_connect(struct vcanfront_dev* dev)
{
	
	char* err;
	struct vcan_sring *sring;
   
	/* Create shared page/ring */
	sring = (struct vcan_sring *)alloc_page();
	if(sring == NULL) {
	  VCANFRONT_ERR("Unable to allocate page for shared memory\n");
	  goto error;	
	}
	memset(sring, 0, PAGE_SIZE);
	dev->ring_ref = gnttab_grant_access(dev->bedomid, virt_to_mfn(sring), 0);
	//~ VCAN_DEBUG("grant ref is %lu\n", (unsigned long) dev->ring_ref);

	/* Initialize shared ring in shared page */
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&dev->ring, sring, PAGE_SIZE);
	
	/* Create event channel for communication with backend */
	if(evtchn_alloc_unbound(dev->bedomid, vcanfront_handler, dev, &dev->comm_evtchn)) {
	  VCANFRONT_ERR("Unable to allocate comm_event channel\n");
	  goto error_postmap;
	}
	vcanfront_evtchn = dev->comm_evtchn;
	unmask_evtchn(dev->comm_evtchn);
	//~ VCAN_DEBUG("comm_event channel is %lu\n", (unsigned long) dev->comm_evtchn);

	

	/* Write the entries to xenstore */
	if(publish_xenbus(dev)) {
	  goto error_postevtchn;
	}

	/* Change state to connected */
	dev->state = XenbusStateConnected;

	/* Tell the backend that we are ready */
	if((err = xenbus_printf(XBT_NIL, dev->nodename, "state", "%u", dev->state))) {
	  VCANFRONT_ERR("Unable to write to xenstore %s/state, value=%u", dev->nodename, XenbusStateConnected);
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

static struct vcanfront_dev* _alloc_vcanfront(const char* _nodename)
{
	struct vcanfront_dev* dev;
	const char* nodename;


	printk("============= Init VCAN Front ================\n");

	dev = malloc(sizeof(*dev));
	if(!dev)
		return NULL;
	
	memset(dev, 0, sizeof(*dev));

	/* Set node name */
	nodename = _nodename ? _nodename : "device/vcan/0";
	dev->nodename = strdup(nodename);
	
	return dev;
}

static struct vcanfront_dev* _init_vcanfront(struct vcanfront_dev* dev)
{
	char *err;
	char path[512];
	
	/* Get backend domid */
	snprintf(path, 512, "%s/backend-id", dev->nodename);
	dev->bedomid = xenbus_read_integer(path);
	VCANFRONT_LOG("backend dom-id is %d\n", dev->bedomid);

	/* Get backend xenstore path */
	snprintf(path, 512, "%s/backend", dev->nodename);
	if((err = xenbus_read(XBT_NIL, path, &dev->bepath))) {
	  VCANFRONT_ERR("Unable to read %s during vcanfront initialization! error = %s\n", path, err);
	  free(err);
	  goto error;
	}
	VCANFRONT_LOG("backend path is %s\n", dev->bepath);

	/* Create and publish grant reference and event channel */
	if (vcanfront_connect(dev)) {
	  goto error;
	}

	/* Wait for backend to connect */
	if( wait_for_backend_state_changed(dev, XenbusStateConnected)) {
	  goto error;
	}

	return dev;

error:
   shutdown_vcanfront(dev);
   return NULL;
}

/**
 * Call this from the application / thread.
 *  - Reads id of backend domain from xenstore
 *  - Reads xenstore path of backend from xenstore
 *  - connects frontend: calls vcanfront_connect()
 *  - waits for backend to connect
 *  --> initialization of frontend done
 */
struct vcanfront_dev* init_vcanfront(const char* nodename)
{
    struct vcanfront_dev* dev;
    
    dev = _alloc_vcanfront(nodename);
    
    if(!dev)
		return NULL;
    
    vcandev = dev;
    
	return _init_vcanfront(dev);
}

void shutdown_vcanfront(struct vcanfront_dev* dev)
{
   char* err;
   char path[512];
   if(dev == NULL) {
      return;
   }
   //~ VCANFRONT_LOG("Shutting down vcanfront\n");
   /* disconnect */
   if(dev->state == XenbusStateConnected) {
      /* Tell backend we are closing */
      dev->state = XenbusStateClosing;
      if((err = xenbus_printf(XBT_NIL, dev->nodename, "state", "%u", (unsigned int) dev->state))) {
	 VCANFRONT_ERR("Unable to write to %s, error was %s", dev->nodename, err);
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
	 VCANFRONT_ERR("Unable to write to %s, error was %s", dev->nodename, err);
	 free(err);
      }

      /* Wait for the backend to close and unmap shared pages, ignore any errors */
      wait_for_backend_state_changed(dev, XenbusStateClosed);

      /* Prepare for a later reopen (possibly by a kexec'd kernel) */
      dev->state = XenbusStateInitialising;
      if((err = xenbus_printf(XBT_NIL, dev->nodename, "state", "%u", (unsigned int) dev->state))) {
		VCANFRONT_ERR("Unable to write to %s, error was %s", dev->nodename, err);
		free(err);
      }

     
   }

   free_vcanfront(dev);
}

void vcanfront_send(struct vcanfront_dev *dev, struct can_frame *cf){
	vcan_request_t req;
	memcpy(&req.frame, cf, sizeof(struct can_frame));
	vcanfront_send_request(dev, &req);
}

int vcanfront_register_rx_handler(struct vcanfront_dev *dev, void (*rx_handler)(struct can_frame*)){
	if(dev->rx_handler)
		return -EBUSY;
		
	dev->rx_handler = rx_handler;
	return 0;
}

void vcanfront_unregister_rx_handler(struct vcanfront_dev *dev){
	dev->rx_handler = NULL;
}

void suspend_vcanfront(void)
{
    mask_evtchn(vcanfront_evtchn);
}

void resume_vcanfront(int rc)
{
	
    // rc == 0: resumed on a new host, so we need to 
    // connect to the new backend
    if (!rc) {	
		_init_vcanfront(vcandev);
    }
    // _init_vcanfront() will already unmask the evtchnS
    // so we only need to do it if we are resumed on the same host
    else{
		unmask_evtchn(vcanfront_evtchn);
	}
}

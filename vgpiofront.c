/*
 * Copyright (c) 2010-2012 United States Government, as represented by
 * the Secretary of Defense.  All rights reserved.
 *
 * This code has been derived from drivers/char/tpm_vtpm.c
 * from the xen 2.6.18 linux kernel
 *
 * Copyright (C) 2006 IBM Corporation
 *
 * This code has also been derived from drivers/char/tpm_xen.c
 * from the xen 2.6.18 linux kernel
 *
 * Copyright (c) 2005, IBM Corporation
 *
 * which was itself derived from drivers/xen/netfront/netfront.c
 * from the linux kernel
 *
 * Copyright (c) 2002-2004, K A Fraser
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */
#include <mini-os/os.h>
#include <mini-os/xenbus.h>
#include <mini-os/xmalloc.h>
#include <mini-os/events.h>
#include <mini-os/wait.h>
#include <mini-os/gnttab.h>
#include <xen/io/xenbus.h>
#include <mini-os/mydevicefront.h>
#include <mini-os/lib.h>
#include <fcntl.h>

#define VGPIOFRONT_PRINT_DEBUG
#ifdef VGPIOFRONT_PRINT_DEBUG
#define VGPIOFRONT_DEBUG(fmt,...) printk("vgpiofront:Debug("__FILE__":%d) " fmt, __LINE__, ##__VA_ARGS__)
#define VGPIOFRONT_DEBUG_MORE(fmt,...) printk(fmt, ##__VA_ARGS__)
#else
#define VGPIOFRONT_DEBUG(fmt,...)
#endif
#define VGPIOFRONT_ERR(fmt,...) printk("vgpiofront:Error " fmt, ##__VA_ARGS__)
#define VGPIOFRONT_LOG(fmt,...) printk("vgpiofront:Info " fmt, ##__VA_ARGS__)

#define min(a,b) (((a) < (b)) ? (a) : (b))


void vgpiofront_send(struct vgpiofront_dev* dev){
	VGPIOFRONT_LOG("sending...");
	if(dev->state == XenbusStateConnected){
		printk("on device %s, via evtchn %u\n", dev->nodename, dev->evtchn);
		wmb();
		int result = notify_remote_via_evtchn(dev->evtchn);		
		VGPIOFRONT_LOG("Sent! (%d)", result);
	}
	else{
		printk("error: not connected");
	}
}

void vgpiofront_handler(evtchn_port_t port, struct pt_regs *regs, void *data) {
   struct vgpiofront_dev* dev = (struct vgpiofront_dev*) data;
   
   VGPIOFRONT_DEBUG("vgpiofront_handler called on port %d\n", port);
   VGPIOFRONT_LOG("shared data: %d\n", dev->page->data);
   
   
   //~ struct vgpiofront_dev* dev = (struct vgpiofront_dev*) data;
   //~ tpmif_shared_page_t *shr = dev->page;
   //~ /*If we get a response when we didnt make a request, just ignore it */
   //~ if(!dev->waiting) {
      //~ return;
   //~ }

   //~ switch (shr->state) {
   //~ case TPMIF_STATE_FINISH: /* request was completed */
   //~ case TPMIF_STATE_IDLE:   /* request was cancelled */
      //~ break;
   //~ default:
      //~ /* Spurious wakeup; do nothing, request is still pending */
      //~ return;
   //~ }

   //~ dev->waiting = 0;
//~ #ifdef HAVE_LIBC
   //~ if(dev->fd >= 0) {
      //~ files[dev->fd].read = 1;
   //~ }
//~ #endif
   //~ wake_up(&dev->waitq);
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

   if((err = xenbus_printf(xbt, dev->nodename, "event-channel", "%u", (unsigned int) dev->evtchn))) {
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
	VGPIOFRONT_DEBUG("connecting...");

   
	/* Create shared page */
	dev->page = (vgpioif_shared_page_t *)alloc_page();
	if(dev->page == NULL) {
	  VGPIOFRONT_ERR("Unable to allocate page for shared memory\n");
	  goto error;
	}
	memset(dev->page, 0, PAGE_SIZE);
	dev->ring_ref = gnttab_grant_access(dev->bedomid, virt_to_mfn(dev->page), 0);
	VGPIOFRONT_DEBUG("grant ref is %lu\n", (unsigned long) dev->ring_ref);

	/*Create event channel */
	if(evtchn_alloc_unbound(dev->bedomid, vgpiofront_handler, dev, &dev->evtchn)) {
	  VGPIOFRONT_ERR("Unable to allocate event channel\n");
	  goto error_postmap;
	}
	unmask_evtchn(dev->evtchn);
	VGPIOFRONT_DEBUG("event channel is %lu\n", (unsigned long) dev->evtchn);

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
      mask_evtchn(dev->evtchn);
      unbind_evtchn(dev->evtchn);
error_postmap:
      gnttab_end_access(dev->ring_ref);
      free_page(dev->page);
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

   dev = malloc(sizeof(struct vgpiofront_dev));
   memset(dev, 0, sizeof(struct vgpiofront_dev));

#ifdef HAVE_LIBC
   dev->fd = -1;
#endif

   nodename = _nodename ? _nodename : "device/vgpio/0";
   dev->nodename = strdup(nodename);

   init_waitqueue_head(&dev->waitq);
   

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
   VGPIOFRONT_LOG("Shutting down vgpiofront\n");
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

      /* Close event channel and unmap shared page */
      mask_evtchn(dev->evtchn);
      unbind_evtchn(dev->evtchn);
      gnttab_end_access(dev->ring_ref);

      free_page(dev->page);
   }

   /* Cleanup memory usage */
   /*if(dev->respbuf) {
      free(dev->respbuf);
   }*/
   if(dev->bepath) {
      free(dev->bepath);
   }
   if(dev->nodename) {
      free(dev->nodename);
   }
   free(dev);
}

//~ int vgpiofront_send(struct vgpiofront_dev* dev, const uint8_t* msg, size_t length)
//~ {
   //~ unsigned int offset;
   //~ tpmif_shared_page_t *shr = NULL;
//~ #ifdef VGPIOFRONT_PRINT_DEBUG
   //~ int i;
//~ #endif
   //~ /* Error Checking */
   //~ if(dev == NULL || dev->state != XenbusStateConnected) {
      //~ VGPIOFRONT_ERR("Tried to send message through disconnected frontend\n");
      //~ return -1;
   //~ }
   //~ shr = dev->page;

//~ #ifdef VGPIOFRONT_PRINT_DEBUG
   //~ VGPIOFRONT_DEBUG("Sending Msg to backend size=%u", (unsigned int) length);
   //~ for(i = 0; i < length; ++i) {
      //~ if(!(i % 30)) {
	 //~ VGPIOFRONT_DEBUG_MORE("\n");
      //~ }
      //~ VGPIOFRONT_DEBUG_MORE("%02X ", msg[i]);
   //~ }
   //~ VGPIOFRONT_DEBUG_MORE("\n");
//~ #endif

   //~ /* Copy to shared pages now */
   //~ offset = sizeof(*shr);
   //~ if (length + offset > PAGE_SIZE) {
      //~ VGPIOFRONT_ERR("Message too long for shared page\n");
      //~ return -1;
   //~ }
   //~ memcpy(offset + (uint8_t*)shr, msg, length);
   //~ shr->length = length;
   //~ barrier();
   //~ shr->state = TPMIF_STATE_SUBMIT;

   //~ dev->waiting = 1;
   //~ dev->resplen = 0;
//~ #ifdef HAVE_LIBC
   //~ if(dev->fd >= 0) {
      //~ files[dev->fd].read = 0;
      //~ files[dev->fd].vgpiofront.respgot = 0;
      //~ files[dev->fd].vgpiofront.offset = 0;
   //~ }
//~ #endif
   //~ wmb();
   //~ notify_remote_via_evtchn(dev->evtchn);
   //~ return 0;
//~ }
//~ int vgpiofront_recv(struct vgpiofront_dev* dev, uint8_t** msg, size_t *length)
//~ {
   //~ unsigned int offset;
   //~ tpmif_shared_page_t *shr = NULL;
//~ #ifdef VGPIOFRONT_PRINT_DEBUG
//~ int i;
//~ #endif
   //~ if(dev == NULL || dev->state != XenbusStateConnected) {
      //~ VGPIOFRONT_ERR("Tried to receive message from disconnected frontend\n");
      //~ return -1;
   //~ }
   //~ /*Wait for the response */
   //~ wait_event(dev->waitq, (!dev->waiting));
   //~ shr = dev->page;

   //~ /* Initialize */
   //~ *msg = NULL;
   //~ *length = 0;
   //~ offset = sizeof(*shr);

   //~ if (shr->state != TPMIF_STATE_FINISH)
      //~ goto quit;

   //~ *length = shr->length;

   //~ if (*length + offset > PAGE_SIZE) {
      //~ VGPIOFRONT_ERR("Reply too long for shared page\n");
      //~ return -1;
   //~ }

   //~ /* Alloc the buffer */
   //~ if(dev->respbuf) {
      //~ free(dev->respbuf);
   //~ }
   //~ *msg = dev->respbuf = malloc(*length);
   //~ dev->resplen = *length;

   //~ /* Copy the bits */
   //~ memcpy(*msg, offset + (uint8_t*)shr, *length);

//~ #ifdef VGPIOFRONT_PRINT_DEBUG
   //~ VGPIOFRONT_DEBUG("Received response from backend size=%u", (unsigned int) *length);
   //~ for(i = 0; i < *length; ++i) {
      //~ if(!(i % 30)) {
	 //~ VGPIOFRONT_DEBUG_MORE("\n");
      //~ }
      //~ VGPIOFRONT_DEBUG_MORE("%02X ", (*msg)[i]);
   //~ }
   //~ VGPIOFRONT_DEBUG_MORE("\n");
//~ #endif
//~ #ifdef HAVE_LIBC
   //~ if(dev->fd >= 0) {
      //~ files[dev->fd].vgpiofront.respgot = 1;
   //~ }
//~ #endif
//~ quit:
   //~ return 0;
//~ }


//~ int vgpiofront_cmd(struct vgpiofront_dev* dev, uint8_t* req, size_t reqlen, uint8_t** resp, size_t* resplen)
//~ {
   //~ int rc;
   //~ if((rc = vgpiofront_send(dev, req, reqlen))) {
      //~ return rc;
   //~ }
   //~ if((rc = vgpiofront_recv(dev, resp, resplen))) {
      //~ return rc;
   //~ }

   //~ return 0;
//~ }

//~ int vgpiofront_set_locality(struct vgpiofront_dev* dev, int locality)
//~ {
   //~ if (!dev || !dev->page)
      //~ return -1;
   //~ dev->page->locality = locality;
   //~ return 0;
//~ }

//~ #ifdef HAVE_LIBC
//~ #include <errno.h>
//~ int vgpiofront_open(struct vgpiofront_dev* dev)
//~ {
   //~ /* Silently prevent multiple opens */
   //~ if(dev->fd != -1) {
      //~ return dev->fd;
   //~ }

   //~ dev->fd = alloc_fd(FTYPE_VGPIOFRONT);
   //~ printk("vgpiofront_open(%s) -> %d\n", dev->nodename, dev->fd);
   //~ files[dev->fd].vgpiofront.dev = dev;
   //~ files[dev->fd].vgpiofront.offset = 0;
   //~ files[dev->fd].vgpiofront.respgot = 0;
   //~ return dev->fd;
//~ }

//~ int vgpiofront_posix_write(int fd, const uint8_t* buf, size_t count)
//~ {
   //~ int rc;
   //~ struct vgpiofront_dev* dev;
   //~ dev = files[fd].vgpiofront.dev;

   //~ if(count == 0) {
      //~ return 0;
   //~ }

   //~ /* Return an error if we are already processing a command */
   //~ if(dev->waiting) {
      //~ errno = EINPROGRESS;
      //~ return -1;
   //~ }
   //~ /* Send the command now */
   //~ if((rc = vgpiofront_send(dev, buf, count)) != 0) {
      //~ errno = EIO;
      //~ return -1;
   //~ }
   //~ return count;
//~ }

//~ int vgpiofront_posix_read(int fd, uint8_t* buf, size_t count)
//~ {
   //~ int rc;
   //~ uint8_t* dummybuf;
   //~ size_t dummysz;
   //~ struct vgpiofront_dev* dev;

   //~ dev = files[fd].vgpiofront.dev;

   //~ if(count == 0) {
      //~ return 0;
   //~ }

   //~ /* get the response if we haven't already */
   //~ if(files[dev->fd].vgpiofront.respgot == 0) {
      //~ if ((rc = vgpiofront_recv(dev, &dummybuf, &dummysz)) != 0) {
	 //~ errno = EIO;
	 //~ return -1;
      //~ }
   //~ }

   //~ /* handle EOF case */
   //~ if(files[dev->fd].vgpiofront.offset >= dev->resplen) {
      //~ return 0;
   //~ }

   //~ /* Compute the number of bytes and do the copy operation */
   //~ if((rc = min(count, dev->resplen - files[dev->fd].vgpiofront.offset)) != 0) {
      //~ memcpy(buf, dev->respbuf + files[dev->fd].vgpiofront.offset, rc);
      //~ files[dev->fd].vgpiofront.offset += rc;
   //~ }

   //~ return rc;
//~ }

//~ int vgpiofront_posix_fstat(int fd, struct stat* buf)
//~ {
   //~ uint8_t* dummybuf;
   //~ size_t dummysz;
   //~ int rc;
   //~ struct vgpiofront_dev* dev = files[fd].vgpiofront.dev;

   //~ /* If we have a response waiting, then read it now from the backend
    //~ * so we can get its length*/
   //~ if(dev->waiting || (files[dev->fd].read == 1 && !files[dev->fd].vgpiofront.respgot)) {
      //~ if ((rc = vgpiofront_recv(dev, &dummybuf, &dummysz)) != 0) {
	 //~ errno = EIO;
	 //~ return -1;
      //~ }
   //~ }

   //~ buf->st_mode = O_RDWR;
   //~ buf->st_uid = 0;
   //~ buf->st_gid = 0;
   //~ buf->st_size = dev->resplen;
   //~ buf->st_atime = buf->st_mtime = buf->st_ctime = time(NULL);

   //~ return 0;
//~ }


//~ #endif

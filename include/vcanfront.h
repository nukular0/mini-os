#ifndef VCANFRONT_H
#define VCANFRONT_H

#include <mini-os/types.h>
#include <mini-os/can.h>
#include <mini-os/os.h>
#include <mini-os/events.h>
#include <mini-os/wait.h>
#include <xen/xen.h>
#include <xen/io/xenbus.h>
#include <xen/io/ring.h>
#include <mini-os/semaphore.h>
//~ #include <mini-os/minios-external/bsd-sys-queue.h>
#include <mini-os/list.h>

struct _pin_irq {
	MINIOS_LIST_ENTRY(struct _pin_irq) list;
	unsigned pin;
	evtchn_port_t port;
	void (*handler)(void);
};


struct vcan_request {
	struct can_frame	frame;
};
typedef struct vcan_request vcan_request_t;

/*
 * Response type for the shared ring.
 * Responses are used to transmit can_frames that were received on 
 * the real (hardware) CAN bus to frontends
 */
struct vcan_response {
	struct can_frame	frame;
};
typedef struct vcan_response vcan_response_t;

DEFINE_RING_TYPES(vcan, struct vcan_request, struct vcan_response);

struct vcanif_shared_page {
    uint32_t length;         /* request/response length in bytes */

    uint8_t data;           /* page data */

};
typedef struct vcanif_shared_page vcanif_shared_page_t;

struct vcanfront_dev {
	grant_ref_t ring_ref;
	evtchn_port_t comm_evtchn;

	struct vcan_front_ring ring;

	domid_t bedomid;
	char* nodename;
	char* bepath;

	XenbusState state;
   
	void (*rx_handler)(struct can_frame*);
};



/*Initialize frontend */
struct vcanfront_dev* init_vcanfront(const char* nodename);
/*Shutdown frontend */
void shutdown_vcanfront(struct vcanfront_dev* dev);


int vcanfront_send_request(struct vcanfront_dev* dev, vcan_request_t *req);

void vcanfront_send(struct vcanfront_dev *dev, struct can_frame *cf);

int vcanfront_register_rx_handler(struct vcanfront_dev *dev, void (*rx_handler)(struct can_frame*));
void vcanfront_unregister_rx_handler(struct vcanfront_dev *dev);

void resume_vcanfront(int rc);
void suspend_vcanfront(void);

#endif

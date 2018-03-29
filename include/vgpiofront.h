#ifndef VGPIOFRONT_H
#define VGPIOFRONT_H

#include <mini-os/types.h>
#include <mini-os/os.h>
#include <mini-os/events.h>
#include <mini-os/wait.h>
#include <xen/xen.h>
#include <xen/io/xenbus.h>
#include <xen/io/ring.h>
#include <mini-os/semaphore.h>

#define CMD_GPIO_REQUEST			0
#define CMD_GPIO_FREE				1
#define CMD_GPIO_DIRECTION_OUTPUT	2	
#define CMD_GPIO_DIRECTION_INPUT	3
#define CMD_GPIO_SET_DEBOUNCE		4
#define CMD_GPIO_GET_VALUE			5
#define CMD_GPIO_SET_VALUE			6

struct vgpio_request {
	unsigned cmd;
	unsigned pin;
	unsigned value; 
};
typedef struct vgpio_request vgpio_request_t;

struct vgpio_response {
	int ret;
};
typedef struct vgpio_response vgpio_response_t;

DEFINE_RING_TYPES(vgpio, struct vgpio_request, struct vgpio_response);

struct vgpioif_shared_page {
    uint32_t length;         /* request/response length in bytes */

    uint8_t data;           /* page data */

};
typedef struct vgpioif_shared_page vgpioif_shared_page_t;

struct vgpiofront_dev {
   grant_ref_t ring_ref;
   evtchn_port_t evtchn;

   struct vgpio_front_ring ring;

   domid_t bedomid;
   char* nodename;
   char* bepath;

   XenbusState state;
   struct semaphore sem; // Semaphore used for waiting for responses from backend
};



/*Initialize frontend */
struct vgpiofront_dev* init_vgpiofront(const char* nodename);
/*Shutdown frontend */
void shutdown_vgpiofront(struct vgpiofront_dev* dev);


int vgpiofront_send(struct vgpiofront_dev* dev);

int gpio_request(unsigned gpio, const char *label);
int gpio_free(unsigned gpio);
int gpio_direction_input(unsigned gpio);
int gpio_direction_output(unsigned gpio, int value);
int gpio_set_debounce(unsigned gpio, unsigned debounce);
int gpio_get_value(unsigned gpio);
void gpio_set_value(unsigned gpio, int value);


#endif

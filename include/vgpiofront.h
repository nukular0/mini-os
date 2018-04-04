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
#include <sys/queue.h>

typedef enum { CMD_GPIO_REQUEST, CMD_GPIO_FREE, CMD_GPIO_DIRECTION_OUTPUT, 
		CMD_GPIO_DIRECTION_INPUT, CMD_GPIO_SET_DEBOUNCE, CMD_GPIO_GET_VALUE, 
		CMD_GPIO_SET_VALUE, CMD_GPIO_REQUEST_IRQ, CMD_GPIO_FREE_IRQ } vgpio_command_t;

#define IRQF_TRIGGER_RISING		0x00000001
#define IRQF_TRIGGER_FALLING	0x00000002
#define IRQF_TRIGGER_HIGH		0x00000004
#define IRQF_TRIGGER_LOW		0x00000008

#define INVALID_RESPONSE			-9999

struct _pin_irq {
	LIST_ENTRY(_pin_irq) list;
	unsigned pin;
	evtchn_port_t port;
	void (*handler)(void);
};


struct vgpio_request {
	vgpio_command_t cmd;
	unsigned pin;
	unsigned val; 
	unsigned irq_edge;
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
   evtchn_port_t comm_evtchn;

   struct vgpio_front_ring ring;

   domid_t bedomid;
   char* nodename;
   char* bepath;

   XenbusState state;
   struct semaphore sem; // Semaphore used for waiting for responses from backend
   
   vgpio_response_t last_response;
   
   LIST_HEAD(pin_irq_list, _pin_irq) irq_list;
};



/*Initialize frontend */
struct vgpiofront_dev* init_vgpiofront(const char* nodename);
/*Shutdown frontend */
void shutdown_vgpiofront(struct vgpiofront_dev* dev);


int vgpiofront_send_request(struct vgpiofront_dev* dev, vgpio_request_t req);

int gpio_request(struct vgpiofront_dev *dev, unsigned gpio, const char *label);
void gpio_free(struct vgpiofront_dev *dev, unsigned gpio);
int gpio_direction_input(struct vgpiofront_dev *dev, unsigned gpio);
int gpio_direction_output(struct vgpiofront_dev *dev, unsigned gpio, int value);
int gpio_set_debounce(struct vgpiofront_dev *dev, unsigned gpio, unsigned debounce);
int gpio_get_value(struct vgpiofront_dev *dev, unsigned gpio);
void gpio_set_value(struct vgpiofront_dev *dev, unsigned gpio, int value);
int gpio_request_irq(struct vgpiofront_dev *dev, unsigned gpio, void (*handler), unsigned trigger);
void gpio_free_irq(struct vgpiofront_dev *dev, unsigned gpio);

#endif

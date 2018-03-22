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
#ifndef MYDEVFRONT_H
#define MYDEVFRONT_H

#include <mini-os/types.h>
#include <mini-os/os.h>
#include <mini-os/events.h>
#include <mini-os/wait.h>
#include <xen/xen.h>
#include <xen/io/xenbus.h>
#include <xen/io/tpmif.h>

struct vgpioif_shared_page {
    uint32_t length;         /* request/response length in bytes */

    uint8_t data;           /* page data */

};
typedef struct vgpioif_shared_page vgpioif_shared_page_t;

struct vgpiofront_dev {
   grant_ref_t ring_ref;
   evtchn_port_t evtchn;

   vgpioif_shared_page_t *page;

   domid_t bedomid;
   char* nodename;
   char* bepath;

   XenbusState state;

   uint8_t waiting;
   struct wait_queue_head waitq;

   uint8_t* respbuf;
   size_t resplen;

#ifdef HAVE_LIBC
   int fd;
#endif

};


/*Initialize frontend */
struct vgpiofront_dev* init_vgpiofront(const char* nodename);
/*Shutdown frontend */
void shutdown_vgpiofront(struct vgpiofront_dev* dev);


void vgpiofront_send(struct vgpiofront_dev* dev);



#endif

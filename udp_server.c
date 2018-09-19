#include <os.h>
#include <xmalloc.h>
#include <console.h>
#include <netfront.h>
#include <lwip/api.h>
#include <mini-os/xenbus.h>

unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long start_ts, end_ts;

void run_client(void *p)
{
	uint64_t t1, t2;
    struct netconn *conn;
    struct netbuf *nb;

    char buf[6];
    char *data;

    struct ip_addr addr;
    unsigned short port = 9935;

    unsigned long period = 20;
    int i = 0;


    xenbus_transaction_t xbt;
    int retry = 0;
    char path[256];
    char *err;

    start_networking();
    addr.addr = htonl(0xc0a80101);

    conn = netconn_new(NETCONN_UDP);
    LWIP_ASSERT("conn != NULL", conn != NULL);
    netconn_bind(conn, IP_ADDR_ANY, 9935);  

    tprintk("Latency Benchmark Client v.1\n");
    tprintk("Period = %lu\n", period);
 
    netconn_connect(conn, &addr, port); 

    snprintf(path, sizeof(path), "data");
    err = xenbus_transaction_start(&xbt);
    if (err) {
        tprintk("starting transaction\n");
        free(err);
    }

    err = xenbus_printf(xbt, path, "ha","%d",0);
    if (err) {
        tprintk("writing ha node\n");
        free(err);
    }
    err = xenbus_transaction_end(xbt, 0, &retry);


    while (1) {
        nb = netbuf_new();
        data = netbuf_alloc(nb, sizeof(buf));
        sprintf(buf, "%i", i++);
        //~ tprintk("%s\n", buf);
        memcpy(data, buf, sizeof(data));

        netconn_send(conn, nb);
        netbuf_delete(nb);
		
		t1 = NOW();
        err = xenbus_transaction_start(&xbt);
        if (err) {
                tprintk("starting transaction for cpsremus\n");
                free(err);
        }

        err = xenbus_printf(xbt, path, "ha", "%s", buf);
        if (err) {
                tprintk("writing to ha\n");
                free(err);
        }
		t2 = NOW();
		
		printk("xenbus_printf took %luus\n", NSEC_TO_USEC(t2-t1));
        err = xenbus_transaction_end(xbt, 0, &retry);
        free(err);

        msleep(period);
    }

    netconn_disconnect(conn);
    do_exit();
}

int app_main(void *p)
{
    create_thread("echoclient", run_client, NULL);
    return 0;
}

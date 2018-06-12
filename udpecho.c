#include <os.h>
#include <xmalloc.h>
#include <console.h>
#include <netfront.h>
#include <lwip/api.h>
#include <xenbus.h>
    
unsigned cycles_low, cycles_high, cycles_low1, cycles_high1; 
unsigned long start_ts, end_ts;

void run_server(void *p)
{
    int iter = 0;
    char buf[512];
    

    struct netconn *conn;
    struct netbuf *nb;

    struct ip_addr *addr;
    unsigned short port;

    char *data;
        volatile int i=0;

    start_networking();
        asm volatile ("RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t": "=r" (cycles_high1), "=r" (cycles_low1):: "%rax", "%rbx", "%rcx", "%rdx");
        end_ts = ( ((unsigned long)cycles_high1 << 32) | cycles_low1 );

    xenbus_printf(XBT_NIL, "data", "ticks", "%lu", end_ts-start_ts);
    printk("Boot time: %lu\n", end_ts-start_ts);
    

    tprintk("Latency Benchmark v.2\n");
    tprintk("Workload: %d iterations. Waiting for packets. \n", iter);

    conn = netconn_new(NETCONN_UDP);
    LWIP_ASSERT("conn != NULL", conn != NULL);

    netconn_bind(conn, NULL, 9935);

    while (1) {
        tprintk("In eventloop of udpecho.c: %i\n",i++);
        nb = netconn_recv(conn);
        addr = netbuf_fromaddr(nb);
        port = netbuf_fromport(nb);

        asm volatile ("CPUID\n\t"
        "RDTSC\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low):: "%rax", "%rbx", "%rcx", "%rdx");
        asm volatile ("RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t": "=r" (cycles_high1), "=r" (cycles_low1):: "%rax", "%rbx", "%rcx", "%rdx");
        asm volatile ("CPUID\n\t"
        "RDTSC\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low):: "%rax", "%rbx", "%rcx", "%rdx");
        asm volatile ("RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t": "=r" (cycles_high1), "=r" (cycles_low1):: "%rax", "%rbx", "%rcx", "%rdx");

        /* disable interrupts xen-guest kernel space */
        //local_irq_save(flags);

        asm volatile ("CPUID\n\t"
        "RDTSC\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low):: "%rax", "%rbx", "%rcx", "%rdx");

        /* disable interrupts and preemption linux kernel space */
        //preempt_disable();
        //raw_local_irq_save(flags);

        /*call the function to measure here*/
        while(i<iter){
           printk("infinite");
           i++;
        }

        asm volatile ("RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t": "=r" (cycles_high1), "=r" (cycles_low1):: "%rax", "%rbx", "%rcx", "%rdx");

        /* enable interrupts xen-guest kernel space */
        //local_irq_restore(flags);
        /* enable interrupts and preemption linux kernel space */
        //raw_local_irq_restore(flags);
        //preempt_enable();

        start_ts = ( ((unsigned long)cycles_high << 32) | cycles_low );
        end_ts = ( ((unsigned long)cycles_high1 << 32) | cycles_low1 );

        netconn_connect(conn, addr, port);

        netbuf_copy(nb, buf, nb->p->tot_len);
        sprintf(buf, "%s %ld", buf, end_ts-start_ts);
        tprintk("%s\n", buf);
        
        data = netbuf_alloc(nb, sizeof(buf));
        memcpy(data, buf, sizeof(buf));

        netconn_send(conn, nb);
        netbuf_delete(nb);
        netconn_disconnect(conn);
    }
    do_exit();
}

int app_main(void *p)
{
    create_thread("echoServer", run_server, NULL);
    return 0;
}

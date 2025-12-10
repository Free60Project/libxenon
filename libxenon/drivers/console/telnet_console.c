#include "telnet_console.h"

#include <lwip/mem.h>
#include <lwip/memp.h>
#include <lwip/sys.h>

#include <lwip/stats.h>

#include <lwip/ip.h>
#include <lwip/udp.h>
#include <lwip/tcp.h>
#include <lwip/dhcp.h>
#include <netif/etharp.h>
#include <network/network.h>
#include <time.h>
#include <xetypes.h>

/*===========================================================================*/
/* Module Definitions                                                        */
/*===========================================================================*/

#define BUFSIZE	65536
#define PORT_TEL 23

typedef struct {
    int len;
    char buf[BUFSIZE];
} TEL_TXST;

enum CONNECTION_STATES {
    TELNET_CONNECTED = 0,
    TELNET_DISCONNECTED,
    TELNET_CLOSING
};

static TEL_TXST tel_st;

static int session_states = TELNET_DISCONNECTED;

static struct tcp_pcb *tel_pcb;

extern void (*stdout_hook)(char *buf, int len);

/**==========================================================================*/
/* Forward Declarations                                                      */
/*===========================================================================*/

static void telnet_send(struct tcp_pcb *pcb, TEL_TXST *st);
static err_t telnet_accept(void *arg, struct tcp_pcb *pcb, err_t err);
static void telnet_close(struct tcp_pcb *pcb);
void telnet_process(void);

/**==========================================================================*/
/* Routines                                                                  */
/*===========================================================================*/

void telnet_console_init(void) {
    err_t err;


    tel_pcb = tcp_new(); /* new tcp pcb */

    if (tel_pcb != NULL) {
        err = tcp_bind(tel_pcb, IP_ADDR_ANY, PORT_TEL); /* bind to port */

        if (err == ERR_OK) {
            tel_pcb = tcp_listen(tel_pcb); /* set listerning */
            tcp_accept(tel_pcb, telnet_accept); /* register callback */
			
			stdout_hook=telnet_console_tx_print;
        }
    }
}

/*===========================================================================*/
void telnet_console_close(void) {
	stdout_hook=NULL;
	
    session_states = TELNET_CLOSING;
	
    telnet_close(tel_pcb); /* close telnet session */
}

/*===========================================================================*/
void telnet_console_tx_print(char *buf, int bc) {
    TEL_TXST *st;

    if (session_states != TELNET_CONNECTED) {
		network_poll();
        return;
    }

	if (buf == NULL || !bc ) {
		return;
	}
	
    st = &tel_st;
    memcpy(st->buf , buf, bc); /* copy data */
    st->len = bc; /* length */
	
    telnet_send(tel_pcb, st); /* send telnet data */
	network_poll();
}


/*===========================================================================*/
static void telnet_close(struct tcp_pcb *pcb) {

    tcp_arg(pcb, NULL); /* clear arg/callbacks */
    tcp_err(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_close(pcb);

    session_states = TELNET_DISCONNECTED;
}

/*===========================================================================*/
static void telnet_send(struct tcp_pcb *pcb, TEL_TXST *st) {
    err_t err;
    u16_t len;


    if (tcp_sndbuf(pcb) < st->len) {
        len = tcp_sndbuf(pcb);
    } else {
        len = st->len;
    }

    do {
        err = tcp_write(pcb, st->buf, len, 1);
        if (err == ERR_MEM) {
            len /= 2;
        }
    } while (err == ERR_MEM && len > 1);

    if (err == ERR_OK) {
        st->len -= len;
    }
}

//recv buffer ...
int telnet_recv_len;
unsigned char telnet_recv_buf[512];

/*===========================================================================*/
static err_t telnet_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {

    if (err == ERR_OK && p != NULL) {
        unsigned char * payload = (unsigned char*) p->payload;

        tcp_recved(pcb, p->tot_len); /* some recieved data */

        //caractere par caractere
        if (p->tot_len == 1) {
            telnet_recv_buf[telnet_recv_len] = payload[0];
            telnet_recv_len++;
        }//par block
        else {
            memcpy(telnet_recv_buf + telnet_recv_len, payload, p->tot_len);
            telnet_recv_len += p->tot_len;
        }

        if (telnet_recv_len > 2) {
            if ((telnet_recv_buf[telnet_recv_len - 2] == '\r')&(telnet_recv_buf[telnet_recv_len - 1] == '\n')) {
                // ParseArgs(telnet_recv_buf, telnet_recv_len);
                // DebugBreak();
                // tel_tx_str(telnet_recv_buf, telnet_recv_len);
                // efface

                // Echo char - optional feedback for the user. All receive buffers will be echoed on a new line.
                // telnet_console_tx_print(telnet_recv_buf, telnet_recv_len);
                
                telnet_recv_len = 0;
                memset(telnet_recv_buf, 0, 512);
            }
        }
    }

    pbuf_free(p); /* dealloc mem */
    if (err == ERR_OK && p == NULL) {
        telnet_close(pcb);
    }

    return ERR_OK;
}

/*===========================================================================*/
static void telnet_err(void *arg, err_t err) {
    printf("telnet_err : %08x\r\n",err);
}

/*===========================================================================*/
static err_t telnet_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    //Client connected
    session_states = TELNET_CONNECTED;

    tel_pcb = pcb;
    tel_st.len = 0;          /* reset length */
    tcp_arg(pcb, &tel_st);      /* argument passed to callbacks */
    tcp_err(pcb, telnet_err);   /* register error callback */
    tcp_recv(pcb, telnet_recv); /* register recv callback */

    return ERR_OK;
}

/*===========================================================================*/
/* End of Module */
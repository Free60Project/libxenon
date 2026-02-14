#ifndef __console_telnet_console_h
#define __console_telnet_console_h

#ifdef __cplusplus
extern "C" {
#endif

void telnet_console_init();
void telnet_console_tx_print(char *buf, int bc);
void telnet_console_close();

// Use these in LibXenon programs to get chars or strings from the telnet buffer.
extern int telnet_recv_len;
extern unsigned char telnet_recv_buf[512];

#ifdef __cplusplus
};
#endif

#endif

#include <stdint.h>
#include <time/time.h>

#define UART_FETCH  ((volatile uint32_t*)0xEA001010)
#define UART_STORE  ((volatile uint32_t*)0xEA001014)
#define UART_STATUS ((volatile uint32_t*)0xEA001018)
#define UART_CNTRL  ((volatile uint32_t*)0xEA00101C)

unsigned char uart_buf[0x1000]; // Storage for bytes fetched
unsigned int uart_pos = 0; // Position in the uart buffer
unsigned int uart_readpos = 0; // Position to read from next

int poll_status()
{
	uint32_t status = 0;

	for(;;)
	{
		status = __builtin_bswap32(*UART_STATUS);
		if(((status & 0xFFFFFFF8) != 0) || ((status & 0x4) != 0))
		{
			asm volatile("db16cyc");
			stall_execution(0xA);
		}
		else
		{
			if((status & 0x1) != 0)
			{
				if(uart_pos == sizeof(uart_buf))
					uart_pos = 0;

				uart_buf[uart_pos++] = __builtin_bswap32(*UART_FETCH);
			}

			break;
		}
	}
	
	return status;
}

int kbhit()
{
	poll_status();

	return (uart_pos != uart_readpos);
}

void putch(unsigned char c)
{
	if (c=='\n') putch('\r');

	// Make sure we can put the char out
	for(;;)
	{
		uint32_t status = poll_status();
		stall_execution(0xA);
		if((status & 0x2) != 0)
			break;
	}

	// Output the char
	*UART_STORE = __builtin_bswap32(c) & 0xFF000000;
}

int getch(void)
{
	while(kbhit() == 0)
		stall_execution(0xA);

	if(uart_readpos == sizeof(uart_buf))
		uart_readpos = 0;

	return uart_buf[uart_readpos++];
}

void uart_puts(unsigned char *s)
{
	while(*s) putch(*s++);
}
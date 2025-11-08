#include <console/console.h>
#include <console/telnet_console.h>
#include <input/input.h>
#include <ppc/cache.h>
#include <ppc/register.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time/time.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_uart/xenon_uart.h>
#include <xenos/xenos.h>
#include <xetypes.h>
#include <usb/usbmain.h>
#include <network/network.h>

#define CPU_STACK_TRACE_DEPTH		10

static char text[4096]="\0";
 
typedef struct _framerec {
	struct _framerec *up;
	void *lr;
} frame_rec, *frame_rec_t;

static int ptr_seems_valid(void * p){
	return (u32)p>=0x80000000 && (u32)p<0xa0000000;
}

/* adapted from libogc exception.c */
static void _cpu_print_stack(void *pc,void *lr,void *r1)
{
	register u32 i = 0;
	register frame_rec_t l,p = (frame_rec_t)lr;

	l = p;
	p = r1;
	
	if (!ptr_seems_valid(p)) return;
	
	sprintf(text,"%s\nSTACK DUMP:",text);

	for(i=0;i<CPU_STACK_TRACE_DEPTH-1 && ptr_seems_valid(p->up);p=p->up,i++) {
		if(i%4) sprintf(text,"%s --> ",text);
		else {
			if(i>0) sprintf(text,"%s -->\n",text);
			else sprintf(text,"%s\n",text);
		}

		switch(i) {
			case 0:
				if(pc) sprintf(text,"%s%p",text,pc);
				break;
			case 1:
				sprintf(text,"%s%p",text,(void*)l);
				break;
			default:
				if(p && p->up) sprintf(text,"%s%p",text,(u32)(p->up->lr));
				break;
		}
	}
}

static void flush_console()
{
	char * p=text;
	while(*p){
		putch(*p);
		console_putch(*p++);
	}

	text[0]='\0';
}

void crashdump(u32 exception,u64 * context)
{
	if(!xenos_is_initialized())
        xenos_init(VIDEO_MODE_AUTO);
    
	console_set_colors(0x000080ff, 0xffffffff);
	console_init();
	console_clrscr();
	printf("Program crashed - reinitializing hardware in this context...\n");
	network_init();
	usb_init();
	network_print_config();
	telnet_console_init();

	// Give some extra time to catch the trace
	mdelay(3000);

	// In case the trace is missed, print it again exactly as it was.
	reprint:

	if (exception){
		sprintf(text,"\nException vector! (%p)\n\n",exception);
	}else{
		strcpy(text,"\nSegmentation fault!\n\n");
	}

	printf(text);
	flush_console();
	
	sprintf(text,"%spir=%016llx dar=%016llx\nsr0=%016llx sr1=%016llx lr=%016llx\n\n",
			text,context[39],context[38],context[36],context[37],context[32]);
	
	printf(text);
	flush_console();

	int i;
	for(i=0;i<8;++i)
		sprintf(text,"%s%02d=%016llx %02d=%016llx %02d=%016llx %02d=%016llx\n",
				text,i,context[i],i+8,context[i+8],i+16,context[i+16],i+24,context[i+24]);
	
	printf(text);
	flush_console();
	
	_cpu_print_stack((void*)(u32)context[36],(void*)(u32)context[32],(void*)(u32)context[1]);

	// This will print to screen for a user.
	strcat(text,"\n\nOn controller: 'x'=Xell, 'y'=Halt, 'b'=Reboot, 'a'=Reprint stack trace.\n");
	strcat(text,"On UART or Telnet: 'x'=Xell, 'h'=Halt, 'r'=Reboot, 'p'=Reprint stack trace.\n");

	printf(text);
	flush_console();

	// Initialize 360 controller - taken from XeLL kbootconf.c
	struct controller_data_s ctrl;
	struct controller_data_s old_ctrl;
	
	// For telnet handling.
	unsigned char latest_telnet_char;

	for(;;){
		// Handle controller
		if (get_controller_data(&ctrl, 0)) {
			if (ctrl.x){
				exit(0);
				for(;;);
				break;
			} if (ctrl.y){
				xenon_smc_power_shutdown();
				for(;;);
				break;
			} if (ctrl.b){
				xenon_smc_power_reboot();
				for(;;);
				break;
			} if (ctrl.a){
				goto reprint;
			}
			old_ctrl=ctrl;
		}

		// Controller update
		usb_do_poll();

		// Telnet update
		network_poll();
		latest_telnet_char = telnet_recv_buf[0];

		// Handle UART
		if(kbhit()){
			switch(getch()){
				case 'x':
					// Try reloading XeLL from NAND. (1f, 2f, gggggg)
					// Platform specific functioanlity defined in libxenon/drivers/newlib/xenon_syscalls.c
					exit(0);
					break;
				case 'h':
					xenon_smc_power_shutdown();
					for(;;);
					break;
				case 'r':
					xenon_smc_power_reboot();
					for(;;);
					break;
				case 'p':
					goto reprint;
			}
		}

		// Handle telnet
		if(latest_telnet_char == 'x'){
			exit(0);
			for(;;);
			break;
		} else if(latest_telnet_char == 'h'){
			xenon_smc_power_shutdown();
			for(;;);
			break;
		} else if(latest_telnet_char == 'r'){
			xenon_smc_power_reboot();
			for(;;);
			break;
		} else if(latest_telnet_char == 'p'){
			goto reprint;
		}
	}
}

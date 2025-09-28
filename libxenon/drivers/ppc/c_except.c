#include <console/console.h>
#include <console/telnet_console.h>
#include <input/input.h>
#include <usb/usbmain.h>
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
    
	// Enable stack dump capture over telnet
	telnet_console_init();

	console_set_colors(0x000080ff, 0xffffffff);
	console_init();
	console_clrscr();
	
	if (exception){
		sprintf(text,"\nException vector! (%p)\n\n",exception);
	}else{
		strcpy(text,"\nSegmentation fault!\n\n");
	}
		
	flush_console();
	
	sprintf(text,"%spir=%016llx dar=%016llx\nsr0=%016llx sr1=%016llx lr=%016llx\n\n",
			text,context[39],context[38],context[36],context[37],context[32]);
	
	flush_console();
	
	int i;
	for(i=0;i<8;++i)
		sprintf(text,"%s%02d=%016llx %02d=%016llx %02d=%016llx %02d=%016llx\n",
				text,i,context[i],i+8,context[i+8],i+16,context[i+16],i+24,context[i+24]);

	flush_console();

	_cpu_print_stack((void*)(u32)context[36],(void*)(u32)context[32],(void*)(u32)context[1]);

	strcat(text,"\n\nOn telnet or uart: 'x'=Soft reboot to XeLL, 'h'=Shutdown, 'r'=Hard reboot\n\nOn controller: 'x'=Soft reboot to XeLL, 'y'=Shutdown, 'b'=Hard Reboot\n\n\n");

	flush_console();

	// September 26 2025 - add more crash handling options.
	// If using telnet you can capture the stack trace over the network instead of needing UART cabling.
	// Caveat: if networking crashed in the program beforehand, YMMV :)

	// Add also controller support to take an action in case of stack dump.
	struct controller_data_s controller;
	struct controller_data_s button;

	for(;;){
		// Read controller input
		if (get_controller_data(&button, 0)){
			if((button.x)&&(!controller.x)){
				exit(0);
				break;
			}
			if((button.y)&&(!controller.y)){
                                xenon_smc_power_shutdown();
                                for(;;);
                                break;
			}
			if((button.b)&&(!controller.b)){
                                xenon_smc_power_reboot();
                                for(;;);
                                break;
			}
			controller=button;
		}
 		usb_do_poll();

		// Read UART char or telnet
		switch(getch()){
			case 'x':
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
		}

	}
}

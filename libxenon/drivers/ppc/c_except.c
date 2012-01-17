#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <console/console.h>
#include <xenon_uart/xenon_uart.h>
#include <xenon_smc/xenon_smc.h>
#include <ppc/cache.h>
#include <ppc/register.h>
#include <xetypes.h>
#include <threads/threads.h>
#include <threads/debug.h>

#define CPU_STACK_TRACE_DEPTH		10

static char text[4096]="\0";
char exception_stack[4096]={0};
 
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
	console_set_colors(0x000080ff, 0xffffffff);
	console_init();
	console_clrscr();
        
        switch(exception)
        {
            case 0x200:
                strcpy(text,"\nMachine Check!\n\n");
                break;
            case 0x380:
                strcpy(text,"\nData SegFault!\n\n");
                break;
            case 0x480:
                strcpy(text,"\nInstruction SegFault!\n\n");
                break;
            case 0x500:
                strcpy(text,"\nExternal Interrupt!\n\n");
                break;
            case 0x600:
                strcpy(text,"\nAlignment!\n\n");
                break;
            case 0x700:
                strcpy(text,"\nProgram Interrupt!\n\n");
                break;
            case 0x800:
                strcpy(text,"\nFPU Unavailable!\n\n");
                break;
            case 0x900:
                strcpy(text,"\nDecrementer!\n\n");
                break;
            case 0x980:
                strcpy(text,"\nHV Decrementer!\n\n");
                break;
            case 0xC00:
                strcpy(text,"\nSystem Call!\n\n");
                break;
            case 0xD00:
                strcpy(text,"\nTrace!\n\n");
                break;
            case 0xE00:
                strcpy(text,"\nFPU Assist!\n\n");
                break;
            case 0xF20:
                strcpy(text,"\nVPU Unavailable!\n\n");
                break;
            case 0x1600:
                strcpy(text,"\nMaintenance!\n\n");
                break;
            case 0x1700:
                strcpy(text,"\nVMX Assist!\n\n");
                break;
            case 0x1800:
                strcpy(text,"\nThermal Managment!\n\n");
                break;
            case 0:
                strcpy(text,"\nSegmentation Fault!\n\n");
                break;
            default:
                sprintf(text,"\nException Vector! (%p)\n\n",exception);
                break;
        }
		
	flush_console();
	
	sprintf(text,"%spir=%016llx dar=%016llx\nIar=%016llx Msr=%016llx lr=%016llx\n\n",
			text,context[39],context[38],context[36],context[37],context[32]);
	
	flush_console();
	
	int i;
	for(i=0;i<8;++i)
		sprintf(text,"%s%02d=%016llx %02d=%016llx %02d=%016llx %02d=%016llx\n",
				text,i,context[i],i+8,context[i+8],i+16,context[i+16],i+24,context[i+24]);
	
	flush_console();
	
	_cpu_print_stack((void*)(u32)context[36],(void*)(u32)context[32],(void*)(u32)context[1]);
	
	strcat(text,"\n\nOn uart: 'x'=Xell, 'h'=Halt, 'r'=Reboot\n\n");

	flush_console();

	for(;;){
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
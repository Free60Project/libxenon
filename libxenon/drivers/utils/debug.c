#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-address"

#include <stdlib.h>
#include <debug.h>
#include <ppc/register.h>
#include <ppc/xenonsprs.h>

void buffer_dump(void * buf, int size){
	int i;
	char* cbuf=(char*) buf;

	printf("[Buffer dump] at %p, size=%d\n",buf,size);
	
	while(size>0){
		for(i=0;i<16;++i){
			printf("%02x ",*cbuf);
			++cbuf;
		}
		printf("\n");
		size-=16;
	}
}

extern unsigned char elfldr_start[]; // also end of .text ...

static int seems_valid(unsigned int p){
	return p>=0x80000000 && p<(unsigned int)elfldr_start;
}

#define DO_RA(x) if(seems_valid(addr) && x<max_depth){ addr=(unsigned int)__builtin_return_address(x); printf("%x; ",addr); }

void stack_trace(int max_depth)
{
    unsigned int addr=0x80000000;
    
    printf("[Stack trace] ");
    
    DO_RA(0);
    DO_RA(1);
    DO_RA(2);
    DO_RA(3);
    DO_RA(4);
    DO_RA(5);
    DO_RA(6);
    DO_RA(7);
    DO_RA(8);
    DO_RA(9);
    DO_RA(10);
    DO_RA(11);
    DO_RA(12);
    DO_RA(13);
    DO_RA(14);
    DO_RA(15);
    
    printf("\n");
}

void data_breakpoint(void * address, int on_read, int on_write)
{
    if (on_read || on_write)
        mtspr(dabrx,6);
    else
        mtspr(dabrx,0);
    
    unsigned int db=((unsigned int)address)&~7;
    
    db|=4; // virtual address
    
    if (on_read)
        db|=1;
    
    if (on_write)
        db|=2;
    
    mtspr(dabr,db);
}

#pragma GCC diagnostic pop

#include <_ansi.h>
#include <_syslist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/iosupport.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/time.h>

#include <assert.h>
#include <debug.h>

#include <ppc/atomic.h>
#include <ppc/register.h>

#include <xenon_soc/xenon_power.h>
#include <xenon_smc/xenon_smc.h>

#define XELL_FOOTER_OFFSET (256*1024-16)
#define XELL_FOOTER_LENGTH 16
#define XELL_FOOTER "xxxxxxxxxxxxxxxx"

extern unsigned char heap_begin;
unsigned char *heap_ptr;

// prototype
static void xenon_exit(int status);
static void *xenon_sbrk(struct _reent *ptr, ptrdiff_t incr);
static int xenon_gettimeofday(struct _reent *ptr, struct timeval *tp, struct timezone *tz);
static void xenon_malloc_unlock(struct _reent *_r);
static void xenon_malloc_lock(struct _reent *_r);

ssize_t vfs_console_write(struct _reent *r, int fd, const char *src, size_t len);

extern void return_to_xell(unsigned int nand_addr, unsigned int phy_loading_addr);
extern void enet_quiesce();
extern void usb_shutdown(void);

//---------------------------------------------------------------------------------
__syscalls_t __syscalls = {
	//---------------------------------------------------------------------------------
	xenon_sbrk, // sbrk
	NULL, // lock_init
	NULL, // lock_close
	NULL, // lock_release
	NULL, // lock_acquire
	xenon_malloc_lock, // malloc_lock
	xenon_malloc_unlock, // malloc_unlock
	xenon_exit, // exit
	xenon_gettimeofday // gettod_r
};


// 22 nov 2005
#define	RTC_BASE	1005782400UL//1132614024UL

static int xenon_gettimeofday(struct _reent *ptr, struct timeval *tp, struct timezone *tz) {
	unsigned char msg[16] = {0x04};
	union{
		uint8_t u8[8];
		uint64_t t;
	} time;
	time.t = 0;
	uint64_t msec = 0;

	xenon_smc_send_message(msg);
	xenon_smc_receive_response(msg);

	time.u8[3] = msg[5];
	time.u8[4] = msg[4];
	time.u8[5] = msg[3];
	time.u8[6] = msg[2];
	time.u8[7] = msg[1];

	msec = (time.t / 1000) + RTC_BASE;	

	tp->tv_sec = (time.t / 1000) + RTC_BASE;
	tp->tv_usec = (time.t % 1000) * 1000;

	return 0;
}

static void *xenon_sbrk(struct _reent *ptr, ptrdiff_t incr) {
	unsigned char *res;
	if (!heap_ptr)
		heap_ptr = &heap_begin;
	res = heap_ptr;
	heap_ptr += incr;
	return res;
}

void shutdown_drivers() {
	// some drivers require a shutdown
	enet_quiesce();
	usb_shutdown();
    data_breakpoint(NULL,0,0);
}

void try_return_to_xell(unsigned int nand_addr, unsigned int phy_loading_addr) {
	if (!memcmp((void*) (nand_addr + XELL_FOOTER_OFFSET), XELL_FOOTER, XELL_FOOTER_LENGTH))
		return_to_xell(nand_addr, phy_loading_addr);
}

static void xenon_exit(int status) {
	char s[256];
	int i, stuck = 0;

	// all threads are down remove syscall for lock and unlock
	__syscalls.malloc_lock = NULL;
	__syscalls.malloc_unlock = NULL;

	// network is down too
	//network_stdout_hook = NULL;

	sprintf(s, "[Exit] with code %d\n", status);
	vfs_console_write(NULL, 0, s, strlen(s));

	for (i = 0; i < 6; ++i) {
		if (xenon_is_thread_task_running(i)) {
			sprintf(s, "Thread %d is still running !\n", i);
			vfs_console_write(NULL, 0, s, strlen(s));
			stuck = 1;
		}
	}

	shutdown_drivers();

	if (stuck) {
		sprintf(s, "Can't reload Xell, looping...");
		vfs_console_write(NULL, 0, s, strlen(s));
	} else {
		sprintf(s, "Reloading Xell...");
		vfs_console_write(NULL, 0, s, strlen(s));
		xenon_set_single_thread_mode();

		try_return_to_xell(0xc8070000, 0x1c000000); // xell-gggggg (ggboot)
		try_return_to_xell(0xc8095060, 0x1c040000); // xell-2f (freeboot)
		try_return_to_xell(0xc8100000, 0x1c000000); // xell-1f, xell-ggggggg
	}

	for (;;);
}


static unsigned int __attribute__((aligned(128))) malloc_spinlock = 0;
static unsigned int __attribute__((aligned(128))) safety_spinlock = 0;
static volatile int lockcount = 0;
static volatile unsigned int lockowner = -1;

static void xenon_malloc_lock(struct _reent *_r) {
    lock(&safety_spinlock);

    int llc=lockcount;
    unsigned int llo=lockowner;
    unsigned int pir_=mfspr(pir);

    assert(llc >= 0);
    
    unlock(&safety_spinlock);
    
    if (llc == 0 || llo != pir_)
    {
        lock(&malloc_spinlock);

        lock(&safety_spinlock);

        ++lockcount;
        lockowner = pir_;

        unlock(&safety_spinlock);
    }
    else
    {
        lock(&safety_spinlock);

        ++lockcount;

        unlock(&safety_spinlock);
    }
}

static void xenon_malloc_unlock(struct _reent *_r) {
    lock(&safety_spinlock);

    int llc=lockcount;
    unsigned int llo=lockowner;

	--lockcount;

	assert(llc > 0);
	assert(llo == mfspr(pir));

    if (llc == 1)
    {
		unlock(&malloc_spinlock);
        lockowner = -1;
    }
        
	unlock(&safety_spinlock);
}

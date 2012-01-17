#include <ppc/timebase.h>
#include <stdint.h>

extern unsigned long long _system_time; // System Timer (Used for telling time)
extern unsigned long long _clock_time; // Clock Timer (Used for timing stuff)
unsigned long long get_system_time()
{
    return _system_time;
}

void set_system_time(unsigned long long time)
{
    _system_time = time;
}

unsigned long long get_clock_time()
{
    return _clock_time;
}

void stall_execution(int i)
{
	i = i * 0x32;
	uint64_t t = mftb() + i;
	while(mftb() < t)
		asm volatile("db16cyc");
}

static void tdelay(uint64_t i)
{
	uint64_t t = mftb();
	t += i;
	while (mftb() < t)
            asm volatile("or %r1, %r1, %r1");
	asm volatile("or %r2, %r2, %r2");
}

void udelay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u / 1000000);
}

void mdelay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u / 1000);
}

void delay(int u)
{
	tdelay(((long long)PPC_TIMEBASE_FREQ) * u);
}

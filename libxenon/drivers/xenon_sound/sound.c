#include <pci/io.h>
#include <stdint.h>
#include <string.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_smc/xenon_gpio.h>
#include <ppc/cache.h>

extern int xenos_is_hdmi;

static int snd_base = 0xea001600, wptr, buffer_len;

// those 2 must be in the first 32MB of physical memory it seems...
static uint8_t buffer[65536] __attribute__ ((section(".bss.beginning.lower"),aligned (256)));
static uint32_t descr[0x20*2] __attribute__ ((section(".bss.beginning.lower"),aligned (256)));

void xenon_sound_init(void)
{
	// reset DAC (init from scratch)
	xenon_gpio_control(5,0x1000,0x1000);
	xenon_gpio_control(0,0x1000,0x1000);
	xenon_gpio_control(4,0x1000,0x1000);

	if(xenos_is_hdmi){
		// HDMI audio enable
		xenon_smc_i2c_write(0x0214, 0x11);
		xenon_smc_i2c_write(0x0202, 0x03);
		xenon_smc_i2c_write(0x0203, 0x00);
		xenon_smc_i2c_write(0x0204, 0x18);
		xenon_smc_i2c_write(0x0205, 0x00);
		xenon_smc_i2c_write(0x021d, 0x40);
		xenon_smc_i2c_write(0x0221, 0x02);
		xenon_smc_i2c_write(0x0222, 0x2b);

		xenon_smc_i2c_write(0x022f,0x01);
		xenon_smc_i2c_write(0x023e,0x3f);

		xenon_smc_i2c_write(0x02df,0x10);// Av mute ?
	}
	
	static unsigned char smc_snd[32] = {0x8d, 1, 1};
	xenon_smc_send_message(smc_snd);

	unsigned int descr_base = ((unsigned int)descr) & 0x1fffffff;
	
	buffer_len = sizeof(buffer);
	memset(buffer, 0, buffer_len);
	memdcbst(buffer, buffer_len);
	
	unsigned int buffer_base = ((unsigned int)buffer) & 0x1fffffff;
	
	int i;
	for (i = 0; i < 0x20; ++i)
	{
		descr[i * 2] = __builtin_bswap32(buffer_base + (buffer_len/0x20) * i);
		descr[i * 2 + 1] = __builtin_bswap32(0x80000000 | (buffer_len/0x20));
	}

	memdcbf(descr, sizeof(descr));

	write32(snd_base + 8, 0);
	write32(snd_base + 8, 0x2000000);
	write32(snd_base + 0, descr_base);
	write32(snd_base + 8, 0x1d08001c);
	write32(snd_base + 0xC, 0x1c);
	
	wptr = 0;
}

void xenon_sound_submit(void *data, int len)
{
	int i = 0;
	while (len)
	{
		int av = buffer_len - wptr;
		if (av > len)
			av = len;
		
		memcpy(buffer + wptr, data + i, av);
		memdcbst(buffer + wptr, av);
		
		i += av;
		wptr += av;
		len -= av;
		if (wptr == buffer_len)
			wptr = 0;
	}
	int cur_descr = (wptr / (buffer_len/0x20) -1) & 0x1f;
	
	write32(snd_base + 4, cur_descr << 8);
	write32(snd_base + 8, read32(snd_base) | 0x1000000);
}

int xenon_sound_get_free(void)
{
	uint32_t reg = read32(snd_base + 4);
	
	int rptr_descr = reg & 0x1f;
	int last_valid_descr = (reg & 0x1f00) >> 8;
	int cur_len = (reg >> 16) & 0xFFFF;
	
	if (rptr_descr == last_valid_descr && !cur_len)
		return buffer_len;

	int rptr = rptr_descr * (buffer_len/0x20);
	int av = rptr - wptr;
	if (av < 0)
		av += buffer_len;
	return av;
}

int xenon_sound_get_unplayed(void)
{
	uint32_t reg = read32(snd_base + 4);
	
	int rptr_descr = reg & 0x1f;
	int last_valid_descr = (reg & 0x1f00) >> 8;
	int cur_len = (reg >> 16) & 0xFFFF;
	
	int l = last_valid_descr - rptr_descr;
	if (l < 0)
		l += 0x20;
	l *= (buffer_len/0x20);
	l += cur_len;
	
	return l;
}

void xenon_tone(uint32_t frequency, uint32_t duration, int16_t amplitude)
{
   uint8_t pcm_data[1024];

   // xenon assumes a 48kHz sample rate for submitted audio data
   const uint32_t sample_rate = 48000;

	uint32_t phase = 0;
	uint32_t phase_step = (uint32_t)(((uint64_t)frequency << 32) / sample_rate);

	const uint32_t bytes_per_frame = sizeof(int16_t) * 2;
	const uint32_t total_frames = (sample_rate * duration) / 1000;
	uint32_t frames_remaining = total_frames;

	while (frames_remaining)
	{
		uint32_t i;
		uint32_t chunk_frames = sizeof(pcm_data) / bytes_per_frame;

		if (chunk_frames > frames_remaining)
			chunk_frames = frames_remaining;

		for (i = 0; i < chunk_frames; ++i)
		{
			// 50% duty square wave
			int16_t v = (phase & 0x80000000U) ? -amplitude : amplitude;
			uint16_t le = (uint16_t)v;
			uint32_t o = i * bytes_per_frame;

			// Write little-endian 16-bit stereo PCM
			pcm_data[o + 0] = le & 0xFF;
			pcm_data[o + 1] = (le >> 8) & 0xFF;
			pcm_data[o + 2] = le & 0xFF;
			pcm_data[o + 3] = (le >> 8) & 0xFF;

			phase += phase_step;
		}

		while (xenon_sound_get_unplayed() >= 32768);

		xenon_sound_submit(pcm_data, sizeof(pcm_data));
		frames_remaining -= chunk_frames;
	}
}

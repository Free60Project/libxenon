#ifndef __xenon_sound_h
#define __xenon_sound_h

#ifdef __cplusplus
extern "C" {
#endif

	/* you init, you get the nr. of LE, 16bit x 2 channels, signed audio bytes to write, and submit that as max. */

void xenon_sound_init(void);
void xenon_sound_submit(void *data, int len);
int xenon_sound_get_free(void);
int xenon_sound_get_unplayed(void);

#define XENON_TONE_AMPLITUDE_100 12000
#define XENON_TONE_AMPLITUDE_50  6000
#define XENON_TONE_AMPLITUDE_25  3000

void xenon_tone(uint32_t frequency, uint32_t duration, int16_t amplitude);

#define xenon_post_beep() xenon_tone(950, 250, XENON_TONE_AMPLITUDE_25)

#ifdef __cplusplus
};
#endif

#endif

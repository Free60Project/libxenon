#ifndef __xenon_sound_h
#define __xenon_sound_h

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize DAC, de-assert AUD_CLAMP, initialize HDMI audio
 */
void xenon_sound_init(void);

/**
 * Submit PCM audio data to be played on the conencted TV, monitor, audio device
 *
 * 48kHz sample rate
 * Signed 16-bit stereo PCM, little endian
 *
 * @param data Pointer to buffer of audio data
 * @param len Number of bytes from the buffer to submit
 */
void xenon_sound_submit(void *data, int len);

/**
 * @return Available free buffer space in bytes.
 */
int xenon_sound_get_free(void);

/**
 * @return Unplayed queued data size in bytes.
 */
int xenon_sound_get_unplayed(void);

/**
 * Generare a square wave tone
 *
 * @param frequency Tone frequency
 * @param duration Tone length in milliseconds.
 * @param amplitude Peak sample amplitude (signed 16-bit).
 */
void xenon_tone(uint32_t frequency, uint32_t duration, int16_t amplitude);

#define XENON_TONE_AMPLITUDE_100 12000
#define XENON_TONE_AMPLITUDE_50  6000
#define XENON_TONE_AMPLITUDE_25  3000

/**
 * Does the classic POST beep that old computers do (950hz tone, 250ms)
 */
#define xenon_post_beep() xenon_tone(950, 250, XENON_TONE_AMPLITUDE_50)

#ifdef __cplusplus
};
#endif

#endif

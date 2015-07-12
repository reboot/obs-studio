#pragma once

#define AVIS_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#ifdef __cplusplus
extern "C" {
#endif

enum AUDIO_WINDOW_TYPES{
	AUDIO_WINDOW_TYPE_RECTANGULAR,
	AUDIO_WINDOW_TYPE_HANNING,
	AUDIO_WINDOW_TYPE_HAMMING,
	AUDIO_WINDOW_TYPE_BLACKMAN,
	AUDIO_WINDOW_TYPE_NUTTALL,
	AUDIO_WINDOW_TYPE_BLACKMAN_NUTTALL,
	AUDIO_WINDOW_TYPE_BLACKMAN_HARRIS
};


struct avis_rect
{
	int x, y;
	int cx, cy;
};

typedef struct avis_rect avis_rect_t;

typedef struct mp_float_buffer mp_float_buffer_t;

void avis_draw_bar(uint32_t *pixels, avis_rect_t *bar, avis_rect_t *area,
	uint32_t color);
void avis_calc_window_coefs(float *buffer, size_t size,
	enum AUDIO_WINDOW_TYPES window_type);

/**
 * @param bins     Array of at least fft window / 2 size
 *                 Pass NULL if only the number of bins needs to be determined
 * @param size     FFT window size
 * @param oct_den  Octave denominator 1 2 3 6 12 24 48 ...
 *                 1 / oct_den cases
 * @return         Number of bins  [bands = bins - 1]
 */
int avis_calc_octave_bins(uint32_t *bins, uint32_t sample_rate, size_t size,
	int oct_den);

#ifdef __cplusplus
}
#endif
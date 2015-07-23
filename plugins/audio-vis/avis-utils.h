/******************************************************************************
Copyright (C) 2015 by HomeWorld <homeworld@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#define AVIS_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#ifdef __cplusplus
extern "C" {
#endif

enum AUDIO_WINDOW_TYPES {
	AUDIO_WINDOW_TYPE_RECTANGULAR,
	AUDIO_WINDOW_TYPE_HANNING,
	AUDIO_WINDOW_TYPE_HAMMING,
	AUDIO_WINDOW_TYPE_BLACKMAN,
	AUDIO_WINDOW_TYPE_NUTTALL,
	AUDIO_WINDOW_TYPE_BLACKMAN_NUTTALL,
	AUDIO_WINDOW_TYPE_BLACKMAN_HARRIS
};

enum AUDIO_WEIGHTING_TYPES {
	AUDIO_WEIGHTING_TYPE_Z,
	AUDIO_WEIGHTING_TYPE_A,
	AUDIO_WEIGHTING_TYPE_C
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
 * @param bins     
 * @param weights  Arrays of at least fft window / 2 size
 *                 Pass NULL if only the number of bins needs to be determined
 * @param size     FFT window size
 * @param oct_den  Octave denominator 1 2 3 6 12 24 48 ...
 *                 1 / oct_den cases
 * @return         Number of bins  [bands = bins - 1]
 */
int avis_calc_octave_bins(uint32_t *bins, float *weights, uint32_t sample_rate,
	size_t size, int oct_den, enum AUDIO_WEIGHTING_TYPES weighting_type);

#ifdef __cplusplus
}
#endif
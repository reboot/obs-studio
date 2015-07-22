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

#include <obs-module.h>
#include "avis-utils.h"


bool avis_rect_intersection(avis_rect_t *a, avis_rect_t *b,
	avis_rect_t *result)
{
	int x = a->x > b->x ? a->x : b->x;
	int y = a->y > b->y ? a->y : b->y;
	int nx = (a->x + a->cx) < (b->x + b->cx) ?
		(a->x + a->cx) : (b->x + b->cx);
	int ny = (a->y + a->cy) < (b->y + b->cy) ?
		(a->y + a->cy) : (b->y + b->cy);
	if (nx >= x && ny >= y)
	{
		result->x = x;
		result->y = y;
		result->cx = nx - x;
		result->cy = ny - y;
		return true;
	}
	else {
		return false;
	}
};

void avis_draw_bar(uint32_t *pixels, avis_rect_t *bar, avis_rect_t *area,
	uint32_t color)
{
	avis_rect_t res;
	int x, y, w, h;

	if (!avis_rect_intersection(bar, area, &res))
		return;

	x = res.x;
	y = res.y;
	w = res.cx;
	h = res.cy;

	for (int i = y; i < y + h; i++)
		for (int j = x; j < x + w; j++)
			pixels[i*area->cx + j] = color;
}

void avis_calc_window_coefs(float *buffer, size_t size,
	enum AUDIO_WINDOW_TYPES window_type)
{
	float a0, a1, a2, a3, a4;

	a0 = a1 = a2 = a3 = a4 = 0.0f;

	if (window_type == AUDIO_WINDOW_TYPE_RECTANGULAR) {
		for (int i = 0; i < size; i++)
			buffer[i] = 1.0f;
		return;
	}

	switch ((int)window_type) {
	case AUDIO_WINDOW_TYPE_HAMMING:
		a0 = 0.53836f;
		a1 = 0.46164f;
		break;
	case AUDIO_WINDOW_TYPE_BLACKMAN:
		a0 = 0.42659f;
		a1 = 0.49656f;
		a2 = 0.076849f;
		break;
	case AUDIO_WINDOW_TYPE_NUTTALL:
		a0 = 0.355768f;
		a1 = 0.487396f;
		a2 = 0.144232f;
		a3 = 0.012604f;
		break;
	case AUDIO_WINDOW_TYPE_BLACKMAN_NUTTALL:
		a0 = 0.3635819f;
		a1 = 0.4891775f;
		a2 = 0.1365995f;
		a3 = 0.0106411f;
		break;
	case AUDIO_WINDOW_TYPE_BLACKMAN_HARRIS:
		a0 = 0.35875f;
		a1 = 0.48829f;
		a2 = 0.14128f;
		a3 = 0.01168f;
		break;
	case AUDIO_WINDOW_TYPE_HANNING:
	default:
		a0 = a1 = 0.5f;
	}

	for (size_t i = 0; i < size; i++) {
		buffer[i] =
			(
			a0
			-
			a1 * cosf((2.0f * M_PI * i) / (size - 1))
			+
			a2 * cosf((4.0f * M_PI * i) / (size - 1))
			-
			a3 * cosf((6.0f * M_PI * i) / (size - 1))
			+
			a4 * cosf((8.0f * M_PI * i) / (size - 1))
			);
	}
}

float aweighting(float frequency)
{
	float ra;
	float  f = frequency;
	float f2 = f * f;
	float n1 = 12200.0f * 12200.0f;
	float n2 = 20.6f * 20.6f;

	ra = (n1 * f2 * f2);
	ra /= (f2 + n2) * (f2 + n1) *
		sqrtf(f2 + 107.7f * 107.7f)
		*
		sqrtf(f2 + 737.9f * 737.9f);
	ra = 2.0f + 20.0f * log10f(ra);
	if (fabs(ra) < 0.002f)
		ra = 0.0f;
	return ra;
}

float cweighting(float frequency)
{
	float ra;
	float  f = frequency;
	float f2 = f * f;
	float n1 = 12200.0f * 12200.0f;
	float n2 = 20.6f * 20.6f;

	ra = (n1 * f2);
	ra /= (f2 + n2) * (f2 + n1);
	ra = 0.06f + 20.0f * log10f(ra);
	if (fabs(ra) < 0.002f)
		ra = 0.0f;
	return ra;
}

float avis_get_bandwidth(uint32_t sampleRate, size_t timeSize)
{
	return (float)sampleRate / timeSize;
}

uint32_t avis_freq_to_bin(float freq, uint32_t sample_rate, size_t size)
{
	return (uint32_t)(freq / avis_get_bandwidth(sample_rate, size));
}

int avis_calc_octave_bins(uint32_t *bins, float *weights, uint32_t sample_rate,
	size_t size, int oct_den, enum AUDIO_WEIGHTING_TYPES weighting_type)
{

	float up_freq, low_freq;
	uint32_t bin_l, bin_u;

	float   center = 31.62777f;
	int      bands = 0;
	float max_freq = sample_rate / 2.0f;
	
	if (max_freq > 16000.0f)
		max_freq = 16000.0f;

	while (center < max_freq)
	{
		up_freq = center * powf(10,
			3.0f / (10.0f * 2 * (float)oct_den));
		low_freq = center / powf(10.0f,
			3.0f / (10 * 2 * (float)oct_den));

		bin_l = avis_freq_to_bin(low_freq, sample_rate, size);
		bin_u = avis_freq_to_bin(up_freq, sample_rate, size);

		if (bin_u > size / 2)
			bin_u = (uint32_t)size / 2 - 1;

		if (center < max_freq && bin_u != bin_l) {
			if (bins) {
				bins[bands] = bin_l;
				switch ((int)weighting_type) {
				case AUDIO_WEIGHTING_TYPE_Z:
					weights[bands] = 0.0f;
					break;
				case AUDIO_WEIGHTING_TYPE_A:
					weights[bands] = aweighting(center);
					break;
				case AUDIO_WEIGHTING_TYPE_C:
					weights[bands] = cweighting(center);
				}
			}
			bands++;
		}
		center = center * powf(10, 3 / (10 * (float)oct_den));
	}

	if (bin_u < size / 2 - 1) {
		if (bins)
			bins[bands] = bin_u;
		bands++;
	}

	return bands;
}

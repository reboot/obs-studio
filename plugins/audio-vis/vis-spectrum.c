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

#include <libavcodec/avfft.h>
#include <util/dstr.h>
#include "vis-spectrum.h"
#include "avis-utils.h"

#define SPECTRUM_WINDOW_SIZE "visual.spectrum.windowsize"
#define SPECTRUM_WINDOW_SIZE_DESC \
		obs_module_text("visual.spectrum.windowsize.desc")

#define SPECTRUM_OCTAVE_FRACT "visual.spectrum.octavefract"
#define SPECTRUM_OCTAVE_FRACT_DESC \
		obs_module_text("visual.spectrum.octavefract.desc")

#define SPECTRUM_WG_TYPE "visual.spectrum.wgtype"
#define SPECTRUM_WG_TYPE_DESC obs_module_text("visual.spectrum.wgtype.desc")

#define SPECTRUM_BG_COLOR "visual.spectrum.bgcolor"
#define SPECTRUM_FG_COLOR "visual.spectrum.fgcolor"

#define SPECTRUM_BG_COLOR_DESC obs_module_text("visual.spectrum.bgcolor.desc")
#define SPECTRUM_FG_COLOR_DESC obs_module_text("visual.spectrum.fgcolor.desc")

struct spectrum_visual
{
	uint32_t       cx;
	uint32_t       cy;

	audio_buffer_t *audio;
	RDFTContext    *rdft_context;
	gs_texture_t   *texture;
	uint8_t        *framebuffer;
	
	float          *window_func;
	int            bins;
	uint32_t       *bins_indexes;
	float          *weights;
	float          *spectrum;

	float          frame_time;
	uint32_t       fg_color;
	uint32_t       bg_color;
};

typedef struct spectrum_visual spectrum_visual_t;

static void * spectrum_create(obs_data_t *settings, uint32_t sample_rate,
	uint32_t channels)
{
	spectrum_visual_t *context = bzalloc(sizeof(spectrum_visual_t));
	
	uint32_t size = (uint32_t)obs_data_get_int(settings,
		SPECTRUM_WINDOW_SIZE);
	
	uint32_t oct_subdiv = (uint32_t)obs_data_get_int(settings,
		SPECTRUM_OCTAVE_FRACT);

	uint32_t weighting_type = (uint32_t)obs_data_get_int(settings,
		SPECTRUM_WG_TYPE);
	
	context->fg_color = (uint32_t)obs_data_get_int(settings,
		SPECTRUM_FG_COLOR);
	context->bg_color = (uint32_t)obs_data_get_int(settings,
		SPECTRUM_BG_COLOR);

	context->audio  = audio_buffer_create(sample_rate, channels, size);

	context->cx = 640;
	context->cy = 360;
	
	int nbits = (int)log2((double)size);
	context->rdft_context = av_rdft_init(nbits, DFT_R2C);
	
	context->framebuffer = bzalloc(context->cx * context->cy * 4);
	
	obs_enter_graphics();
	context->texture = gs_texture_create(context->cx, context->cy,
		GS_RGBA, 1, NULL, GS_DYNAMIC);
	obs_leave_graphics();

	context->window_func = bzalloc(size * sizeof(float));
	
	avis_calc_window_coefs(context->window_func, size,
		AUDIO_WINDOW_TYPE_HANNING);

	int bins = avis_calc_octave_bins(NULL, NULL, sample_rate, size,
		oct_subdiv, weighting_type);

	context->bins         = bins;
	context->bins_indexes = bzalloc((bins + 1) * sizeof(uint32_t));
	context->weights      = bzalloc((bins + 1) * sizeof(float));
	context->spectrum     = bzalloc((bins + 1) * sizeof(float));
	
	avis_calc_octave_bins(context->bins_indexes, context->weights,
		sample_rate, size, oct_subdiv, weighting_type);
	
	return context;
}

static void spectrum_destroy(void *data)
{
	spectrum_visual_t *context = (spectrum_visual_t *)data;
	
	if (!context)
		return;

	audio_buffer_destroy(context->audio);

	bfree(context->framebuffer);

	obs_enter_graphics();
	gs_texture_destroy(context->texture);
	obs_leave_graphics();
	
	av_rdft_end(context->rdft_context);
	
	bfree(context->window_func);
	bfree(context->bins_indexes);
	bfree(context->weights);
	bfree(context->spectrum);
	
	bfree(context);
}

static void spectrum_render(void *data, gs_effect_t *effect)
{
	spectrum_visual_t *context = (spectrum_visual_t *)data;
	
	if (!context) return;

	gs_reset_blend_state();

	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"),
		context->texture);

	gs_draw_sprite(context->texture, 0, context->cx, context->cy);
}

static void spectrum_draw(spectrum_visual_t *context);

static void spectrum_tick(void *data, float seconds)
{
	spectrum_visual_t *context = (spectrum_visual_t *)data;
	
	context->frame_time = seconds;
	
	spectrum_draw(context);
}

static void spectrum_process_audio(void *data, audio_buffer_t *audio)
{
	spectrum_visual_t *context = (spectrum_visual_t *)data;
	
	if (!context) return;
	if (!context->audio) return;

	size_t size  = context->audio->size;

	__m128 vol  = _mm_set1_ps(audio->volume);

	for (uint32_t i = 0; i < context->audio->channels; i++) {
		float *dst = context->audio->buffer[i];
		float *src = audio->buffer[i];
		float *wf = context->window_func;
		for (float *d = dst, *s = src;
			d < dst + size;
			d += 4, s += 4, wf += 4) {
			__m128 wc = _mm_load_ps(wf);
			_mm_store_ps(
				d,
				_mm_mul_ps(
				_mm_load_ps(s), _mm_mul_ps(wc, vol))
				);
		}
	}
}

void spectrum_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SPECTRUM_WINDOW_SIZE, 1024);
	obs_data_set_default_int(settings, SPECTRUM_OCTAVE_FRACT, 3);
	obs_data_set_default_int(settings, SPECTRUM_FG_COLOR, 0xFF00FFFF);
	obs_data_set_default_int(settings, SPECTRUM_BG_COLOR, 0x00000000);
	obs_data_set_default_int(settings, SPECTRUM_WG_TYPE, 1);
}

#define SP_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static void spectrum_get_properties(void *data, obs_properties_t *props)
{
	spectrum_visual_t     *context = (spectrum_visual_t *)data;
	obs_property_t   *prop;

	prop = obs_properties_add_list(
		props,
		SPECTRUM_WINDOW_SIZE,
		SPECTRUM_WINDOW_SIZE_DESC,
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	struct dstr str = { 0 };
	int ws[] = { 512, 1024, 2048, 4096 };

	for (int i = 0; i < SP_ARRAY_LEN(ws); i++) {
		dstr_printf(&str, "%d", ws[i]);
		obs_property_list_add_int(prop, str.array, ws[i]);
	}

	prop = obs_properties_add_list(
		props,
		SPECTRUM_OCTAVE_FRACT,
		SPECTRUM_OCTAVE_FRACT_DESC,
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	int ofd[] = { 1, 3, 6, 12 };

	for (int i = 0; i < SP_ARRAY_LEN(ofd); i++) {
		dstr_printf(&str, "1 / %d", ofd[i]);
		obs_property_list_add_int(prop, str.array, ofd[i]);
	}

	dstr_free(&str);

	const char *wgtype[] = { "Z (Flat)", "A" };

	prop = obs_properties_add_list(
		props,
		SPECTRUM_WG_TYPE,
		SPECTRUM_WG_TYPE_DESC,
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	for (int i = 0; i < 2; i++)
		obs_property_list_add_int(prop, wgtype[i], i);

	obs_properties_add_color(props, SPECTRUM_FG_COLOR,
		SPECTRUM_FG_COLOR_DESC);
	obs_properties_add_color(props, SPECTRUM_BG_COLOR,
		SPECTRUM_BG_COLOR_DESC);
}

static uint32_t spectrum_get_width(void *data)
{
	spectrum_visual_t *context = (spectrum_visual_t *)data;
	return context->cx;
}

static uint32_t spectrum_get_height(void *data)
{
	spectrum_visual_t *context = (spectrum_visual_t *)data;
	return context->cy;
}

static size_t spectrum_frame_size(void *data)
{
	spectrum_visual_t *context = (spectrum_visual_t *)data;
	return context->audio->size;
}

static void spectrum_process_fft(spectrum_visual_t *context)
{
	if (!context) return;

	if (!context->audio) return;

	if (!context->rdft_context) return;

	audio_buffer_t *audio = context->audio;
	uint32_t           ch = audio->channels;
	size_t             ws = audio->size;
	
	for (uint32_t i = 0; i < ch; i++) {
		if (audio->buffer[i]) {
			av_rdft_calc(context->rdft_context,
				audio->buffer[i]);
		}
	}

	__m128 wsize = _mm_set_ps1((float)ws * 0.5f);

	for (uint32_t i = 0; i < ch; i++) {
		float *buffer = audio->buffer[i];
		for (uint32_t j = 0; j < ws; j += 4) {
			_mm_store_ps(buffer + j,
				_mm_div_ps(_mm_load_ps(buffer + j), wsize));
		}
	}
}

static inline void sp_memset32(uint32_t *pixels, uint32_t val, size_t size)
{
	uint32_t *addr = pixels;
	for (uint32_t *d = addr; d < addr + size; d++)
		*(d) = val;
}

#define DB_MIN -70.0f

static void spectrum_draw(spectrum_visual_t *context)
{
	if (!context) return;
	if (!context->texture) return;

	uint32_t channels = context->audio->channels;
	size_t       size = context->audio->size;
	uint32_t       cx = context->cx;
	uint32_t       cy = context->cy;
	uint32_t fft_size = (uint32_t)size / 2;
	
	uint32_t *framebuffer = (uint32_t *)context->framebuffer;
	
	spectrum_process_fft(context);

	sp_memset32(framebuffer, context->bg_color, cx * cy);
	
	uint32_t bins = context->bins;
	uint32_t  *bi = context->bins_indexes;

	for (uint32_t b = 0; b < bins; b++) {
		uint32_t start = bi[b] + 1;
		uint32_t  stop = bi[b + 1] + 1;
		float mag = 0;
		for (uint32_t nch = 0; nch < channels; nch++) {
			float *buffer = context->audio->buffer[nch];
			float bmag = 0;

			if (!buffer)
				break;

			for (uint32_t o = start; o < stop; o++) {
				float re, im;
				re = buffer[o * 2];
				im = buffer[o * 2 + 1];
				bmag += re * re + im * im;
			}
			mag += bmag / (stop - start);
		}

		mag /= (float)channels;
		mag = 10.0f * log10f(mag);
		mag += context->weights[b];

		if (mag < DB_MIN)
			mag = DB_MIN;

		mag = (DB_MIN - mag) / DB_MIN;

		if (context->spectrum[b] > mag) {
			context->spectrum[b] -=
				(context->spectrum[b] - mag) *
				context->frame_time * 10.0f;
		}
		else {
			context->spectrum[b] = mag;
		}
	}

	uint32_t bands = bins - 1;
	uint32_t w = cx;
	uint32_t h = cy;

	int fbw = w / bands;
	int pad = (w - bands * fbw) / 2;
	int bpad = (int)((float)fbw * 0.2f);
	int bw = fbw - bpad;
	avis_rect_t view = { 0, 0, w, h };

	for (uint32_t b = 0; b < bands; b++) {
		int bh = (int)(h * context->spectrum[b]);
		int by = (int)h - bh;
		avis_rect_t bar = { pad + fbw * b, by, bw, bh };

		avis_draw_bar(framebuffer, &bar, &view,
			context->fg_color);
	}

	obs_enter_graphics();
	gs_texture_set_image(context->texture, context->framebuffer,
		cx * 4, false);
	obs_leave_graphics();
}




static visual_t spectrum_visual = {
	.create         = spectrum_create,
	.destroy        = spectrum_destroy,
	.render         = spectrum_render,
	.tick           = spectrum_tick,
	.process_audio  = spectrum_process_audio,
	.get_defaults   = spectrum_get_defaults,
	.get_properties = spectrum_get_properties,
	.get_width      = spectrum_get_width,
	.get_height     = spectrum_get_height,
	.frame_size     = spectrum_frame_size
};


visual_t * register_spectrum_visualisation(void)
{
	return &spectrum_visual;
}

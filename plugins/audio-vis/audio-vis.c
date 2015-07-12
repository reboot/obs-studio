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
#include <libavcodec/avfft.h>
#include <libavutil/avutil.h>
#include <util/threading.h>
#include <util/dstr.h>

#include "avis-utils.h"

#define blog(log_level, format, ...) \
	blog(log_level, "[AVIS Source: '%s'] " format, \
			obs_source_get_name(context->source), ##__VA_ARGS__)

#define debug(format, ...) \
	blog(LOG_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...) \
	blog(LOG_INFO, format, ##__VA_ARGS__)
#define warn(format, ...) \
	blog(LOG_WARNING, format, ##__VA_ARGS__)

#define AVIS_PLUGIN_NAME  obs_module_text("AVIS.PluginName")

#define SETTINGS_AUDIO_SOURCES  "AVIS.AudioSources"
#define TEXT_AUDIO_SOURCES_DESC obs_module_text("AVIS.AudioSourcesDesc")

#define SETTINGS_FFT_SIZE  "AVIS.FFTSize"
#define TEXT_FFT_SIZE_DESC obs_module_text("AVIS.FFTSizeDesc")

#define SETTINGS_OCTAVE_FRACT "AVIS.OctaveFract"
#define TEXT_OCTAVE_FRACT_DESC obs_module_text("AVIS.OctaveFractDesc")

#define SETTINGS_BG_COLOR "AVIS.BgColor"
#define SETTINGS_FG_COLOR "AVIS.FgColor"

#define TEXT_BG_COLOR_DESC obs_module_text("AVIS.BgColorDesc")
#define TEXT_FG_COLOR_DESC obs_module_text("AVIS.FgColorDesc")

#define SETTINGS_WGHT_TYPE "AVIS.WeightingType"
#define TEXT_WGHT_TYPE_DESC obs_module_text("AVIS.WeightingTypeDesc")

#define DB_MIN -70.0f
#define ACQ_RETRY_TIMEOUT_S 1.0f
/**
 * TODO  Switch avis_audio and avis_fft to mp_float_buffer
 */

struct avis_audio {
	float    *buffers[MAX_AV_PLANES];
	uint32_t channels;
	size_t   window_size;
	float    volume;

};

typedef struct avis_audio avis_audio_t;

struct avis_fft {
	float        *fft_buffers[MAX_AV_PLANES];
	float        *window_coefs;
	uint32_t     channels;
	size_t       window_size;
	uint32_t     sample_rate;
	RDFTContext  *rdft_context;
};

typedef struct avis_fft avis_fft_t;

struct audiovis_source {
	obs_source_t    *source;
	obs_source_t    *audio_source;
	const char      *audio_source_name;
	float           acq_retry_timeout;

	avis_audio_t    *audio_context;
	avis_fft_t      *fft_context;

	size_t          window_size;

	gs_texture_t    *tex;
	uint8_t         *framebuffer;
	uint32_t        cx;
	uint32_t        cy;

	float           global_time;
	float		frame_time;
	bool            can_render;
	
	uint32_t        bins;
	uint32_t        oct_subdiv;
	uint32_t        *bins_indexes;
	float           *spectrum;
	float           *spec_peaks;
	float           *weights;
	uint32_t        weighting_type;

	uint32_t        fg_color;
	uint32_t        bg_color;
	bool            visible;

	pthread_mutex_t audio_mutex;
};

static void alloc_fft_buffers(avis_fft_t *fft_ctx)
{
	if (!fft_ctx)
		return;

	for (uint32_t i = 0; i < fft_ctx->channels; i++)
		fft_ctx->fft_buffers[i] =
			bzalloc(fft_ctx->window_size * sizeof(float));

	fft_ctx->window_coefs = 
		bzalloc(fft_ctx->window_size * sizeof(float));
}

static void free_fft_buffers(avis_fft_t *fft_ctx)
{
	if (!fft_ctx)
		return;

	for (uint32_t i = 0; i < fft_ctx->channels; i++)
		bfree(fft_ctx->fft_buffers[i]);

	bfree(fft_ctx->window_coefs);
}

static void alloc_audio_buffers(avis_audio_t *audio_ctx)
{
	if (!audio_ctx)
		return;

	for (uint32_t i = 0; i < audio_ctx->channels; i++)
		audio_ctx->buffers[i] =
			bzalloc(audio_ctx->window_size * sizeof(float));
}

static void free_audio_buffers(avis_audio_t *audio_ctx)
{
	if (!audio_ctx)
		return;

	for (uint32_t i = 0; i < audio_ctx->channels; i++)
		bfree(audio_ctx->buffers[i]);

}

static void avis_release_audio_context(struct audiovis_source *context)
{
	if (!context)
		return;
	
	pthread_mutex_lock(&context->audio_mutex);

	if (context->audio_context)
		free_audio_buffers(context->audio_context);

	bfree(context->audio_context);
	context->audio_context = NULL;

	pthread_mutex_unlock(&context->audio_mutex);
}

static void avis_release_fft_context(struct audiovis_source *context)
{
	if (!context)
		return;

	if (context->fft_context) {
		free_fft_buffers(context->fft_context);
		if (context->fft_context->rdft_context) {
			av_rdft_end(context->fft_context->rdft_context);
		}
	}

	bfree(context->fft_context);
	context->fft_context = NULL;
}

static void avis_init_fft_context(struct audiovis_source *context,
				uint32_t sample_rate,
				uint32_t channels,
				size_t window_size,
				enum AUDIO_WINDOW_TYPES window_type)
{
	if (!context)
		return;

	if (context->fft_context) {
		uint32_t nch = context->fft_context->channels;
		size_t wsize = context->fft_context->window_size;
		if (nch != channels || wsize != window_size) {
			avis_release_fft_context(context);
		}
		else {
			return;
		}
	}

	context->fft_context = bzalloc(sizeof(avis_fft_t));
	context->fft_context->channels    = channels;
	context->fft_context->window_size = window_size;
	context->fft_context->sample_rate = sample_rate;


	alloc_fft_buffers(context->fft_context);
	avis_calc_window_coefs(context->fft_context->window_coefs,
		window_size, window_type);

	int nbits = (int)log2((double)window_size);
	context->fft_context->rdft_context = av_rdft_init(nbits, DFT_R2C);
}


static void avis_init_audio_context(struct audiovis_source *context,
				uint32_t channels,
				size_t window_size)
{
	if (!context)
		return;

	if (context->audio_context) {
		uint32_t nch = context->audio_context->channels;
		size_t wsize = context->audio_context->window_size;
		if (nch != channels || wsize != window_size) {
			avis_release_audio_context(context);
		}
		else {
			return;
		}
	}

	pthread_mutex_lock(&context->audio_mutex);

	context->audio_context = bzalloc(sizeof(avis_audio_t));
	context->audio_context->channels = channels;
	context->audio_context->window_size = window_size;
	
	alloc_audio_buffers(context->audio_context);

	pthread_mutex_unlock(&context->audio_mutex);
}

static void avis_copy_buffers_and_apply_window(struct audiovis_source *context)
{
	avis_fft_t   *fft_context;
	avis_audio_t *audio_context;
	size_t       window_size;
	float        volume;

	if (!context)
		return;

	if(pthread_mutex_trylock(&context->audio_mutex) == EBUSY)
		return;
	
	fft_context   = context->fft_context;
	audio_context = context->audio_context;
	window_size   = fft_context->window_size;
	volume        = audio_context->volume;
	
	__m128 vol    = _mm_set1_ps(volume);

	for (uint32_t i = 0; i < fft_context->channels; i++) {
		float *dst   = fft_context->fft_buffers[i];
		float *src   = audio_context->buffers[i];
		float *wcfs = fft_context->window_coefs;
		for (float *d = dst, *s = src;
			d < dst + window_size;
			d += 4, s += 4, wcfs += 4) {
			__m128 wc = _mm_load_ps(wcfs);
			_mm_store_ps(
				d,
				_mm_mul_ps(
				_mm_load_ps(s),_mm_mul_ps(wc, vol))
				);
		}
	}

	pthread_mutex_unlock(&context->audio_mutex);
}

static void avis_audio_source_data_received_signal(void *vptr,
	calldata_t *calldata)
{
	struct audiovis_source *context = vptr;
	struct audio_data *data = calldata_ptr(calldata, "data");
	size_t frames, window_size, offset;
	uint32_t channels;

	if (!context)
		return;
	if (!context->audio_context)
		return;
	
	if (pthread_mutex_trylock(&context->audio_mutex) == EBUSY)
		return;

	frames      = data->frames;
	window_size = context->audio_context->window_size;
	offset      = window_size - frames;

	context->audio_context->volume = data->volume;

	if (frames > window_size)
		return;

	channels = context->audio_context->channels;

	for (uint32_t i = 0; i < channels; i++) {
		float *abuffer = context->audio_context->buffers[i];
		float *adata   = (float*)data->data[i];
		if (adata) {
			memmove(abuffer,
				abuffer + frames,
				offset * sizeof(float));

			memcpy(abuffer + offset,
				adata,
				frames * sizeof(float));
		}
	}

	pthread_mutex_unlock(&context->audio_mutex);
}

static const char *audiovis_source_get_name(void)
{
	return AVIS_PLUGIN_NAME;
}

static void avis_release_texture(struct audiovis_source *context)
{
	if (!context)
		return;

	if (context->tex) {
		obs_enter_graphics();

		gs_texture_destroy(context->tex);
		bfree(context->framebuffer);

		context->tex = NULL;
		context->framebuffer = NULL;

		obs_leave_graphics();
	}
}

static void avis_init_texture(struct audiovis_source *context)
{
	uint32_t cx, cy;

	if (!context)
		return;

	cx = context->cx;
	cy = context->cy;

	if (context->tex){
		uint32_t tw = gs_texture_get_width(context->tex);
		uint32_t th = gs_texture_get_height(context->tex);
		if (cx != tw || cy != th)
			avis_release_texture(context);
	}

	if (!context->tex) {
		obs_enter_graphics();
		context->tex = gs_texture_create(
			cx,
			cy,
			GS_RGBA, 1, NULL, GS_DYNAMIC);
		context->framebuffer = bzalloc(cx * cy * 4);
		obs_leave_graphics();
	}
}


static void avis_release_audio_source(struct audiovis_source *context);

static void avis_audio_source_removed_signal(void *vptr, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);

	struct audiovis_source *context = vptr;
	
	if (!context)
		return;

	avis_release_audio_source(context);
}

static void avis_release_audio_source(struct audiovis_source *context)
{
	if (!context)
		return;

	if (context->audio_source) {
		signal_handler_t *sh;

		sh = obs_source_get_signal_handler(context->audio_source);

		pthread_mutex_lock(&context->audio_mutex);
		signal_handler_disconnect(sh, "audio_data",
			avis_audio_source_data_received_signal, context);
		pthread_mutex_unlock(&context->audio_mutex);

		signal_handler_disconnect(sh, "remove",
			avis_audio_source_removed_signal, context);

		obs_source_release(context->audio_source);
		context->audio_source = NULL;
		context->can_render = false;
		context->acq_retry_timeout = ACQ_RETRY_TIMEOUT_S;
	}
}

static void avis_acquire_audio_source(struct audiovis_source *context)
{
	if (!context)
		return;

	bool global_source_found   = false;
	const char *source_name    = context->audio_source_name;
	obs_source_t *audio_source = NULL;
	signal_handler_t *sh;

	for (uint32_t i = 1; i <= 10; i++) {
		obs_source_t *source = obs_get_output_source(i);
		if (source) {
			uint32_t flags = obs_source_get_output_flags(source);
			if (flags & OBS_SOURCE_AUDIO) {
				const char *name = obs_source_get_name(source);
				if (strcmp(name, source_name) == 0) {
					global_source_found = true;
					audio_source = source;
					break;
				}

			}
			obs_source_release(source);
		}
	}

	if (!global_source_found)
		audio_source = obs_get_source_by_name(source_name);

	if (!audio_source)
		return;

	if (audio_source == context->audio_source) {
		obs_source_release(audio_source);
		return;
	}
	
	avis_release_audio_source(context);

	context->audio_source = audio_source;
	
	blog(LOG_INFO, "Source acquired: %s", source_name);

	sh = obs_source_get_signal_handler(audio_source);
	
	signal_handler_connect(sh, "audio_data",
		avis_audio_source_data_received_signal, context);
	signal_handler_connect(sh, "remove",
		avis_audio_source_removed_signal, context);
	
	context->can_render = true;
}


static void avis_update_settings(struct audiovis_source *context,
			obs_data_t *settings)
{
	context->audio_source_name = obs_data_get_string(settings,
		SETTINGS_AUDIO_SOURCES);

	context->window_size = obs_data_get_int(settings,
		SETTINGS_FFT_SIZE);

	context->oct_subdiv = (uint32_t)obs_data_get_int(settings,
		SETTINGS_OCTAVE_FRACT);

	context->fg_color = (uint32_t)obs_data_get_int(settings,
		SETTINGS_FG_COLOR);

	context->bg_color = (uint32_t)obs_data_get_int(settings,
		SETTINGS_BG_COLOR);

	context->weighting_type = (uint32_t)obs_data_get_int(settings,
		SETTINGS_WGHT_TYPE);

	context->cx = 640;
	context->cy = 360;
}

static void audiovis_source_update(void *data, obs_data_t *settings)
{
	struct audiovis_source *context = data;
	struct obs_audio_info  oai;
	uint32_t channels, sample_rate, bins, oct_subdiv, weighting_type;
	size_t   window_size;

	if (!context)
		return;

	avis_update_settings(context, settings);

	avis_init_texture(context);

	obs_get_audio_info(&oai);

	channels    = get_audio_channels(oai.speakers);
	sample_rate = oai.samples_per_sec;
	window_size = context->window_size;
	oct_subdiv  = context->oct_subdiv;
	weighting_type = context->weighting_type;

	bins = context->bins;

	context->bins = avis_calc_octave_bins(NULL, NULL,
		sample_rate, window_size, oct_subdiv, weighting_type);

	
	if (bins != context->bins) {
		if (context->spectrum)
			bfree(context->spectrum);
		if (context->bins_indexes)
			bfree(context->bins_indexes);
		if (context->spec_peaks)
			bfree(context->spec_peaks);
		if (context->weights)
			bfree(context->weights);
		context->spectrum = NULL;
		context->bins_indexes = NULL;
		context->spec_peaks = NULL;
		context->weights = NULL;
	}
	
	bins = context->bins;

	if (!context->spectrum)
		context->spectrum = bzalloc((bins + 1) * sizeof(float));
	if (!context->bins_indexes)
		context->bins_indexes = bzalloc((bins +1) *sizeof(uint32_t));
	if (!context->spec_peaks)
		context->spec_peaks = bzalloc((bins + 1) * sizeof(float));
	if (!context->weights)
		context->weights = bzalloc((bins + 1) * sizeof(float));

	avis_calc_octave_bins(context->bins_indexes, context->weights,
		sample_rate, window_size, oct_subdiv, weighting_type);


	avis_init_audio_context(context, channels, window_size);
	avis_init_fft_context(context, sample_rate, channels, window_size,
		AUDIO_WINDOW_TYPE_HANNING);
	
	avis_acquire_audio_source(context);
}


static void *audiovis_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct audiovis_source *context;

	context = bzalloc(sizeof(struct audiovis_source));
	context->source = source;
	pthread_mutex_init(&context->audio_mutex, NULL);
	context->acq_retry_timeout = ACQ_RETRY_TIMEOUT_S;
	audiovis_source_update(context, settings);

	return context;
}

static void audiovis_source_destroy(void *data)
{
	struct audiovis_source *context = data;

	avis_release_audio_source(context);
	avis_release_audio_context(context);
	avis_release_fft_context(context);
	avis_release_texture(context);

	if (context->spectrum)
		bfree(context->spectrum);
	
	if (context->bins_indexes)
		bfree(context->bins_indexes);
	
	if (context->spec_peaks)
		bfree(context->spec_peaks);

	if (context->weights)
		bfree(context->weights);

	pthread_mutex_destroy(&context->audio_mutex);
	
	bfree(context);
}


static uint32_t audiovis_source_getwidth(void *data)
{
	struct audiovis_source *context = data;
	return context->cx;
}

static uint32_t audiovis_source_getheight(void *data)
{
	struct audiovis_source *context = data;
	return context->cy;
}

static void audiovis_source_show(void *data)
{
	struct audiovis_source *context = data;
	context->visible = true;
}

static void audiovis_source_hide(void *data)
{
	struct audiovis_source *context = data;
	context->visible = false;
}

static void audiovis_source_activate(void *data)
{
	struct audiovis_source *context = data;
}

static void audiovis_source_deactivate(void *data)
{
	struct audiovis_source *context = data;
}


static void avis_process_fft(struct audiovis_source *context)
{
	uint32_t ch;
	size_t   ws;
	
	if (!context)
		return;

	if (!context->fft_context)
		return;

	avis_fft_t *fft_ctx = context->fft_context;

	if (!fft_ctx->rdft_context)
		return;

	ch = fft_ctx->channels;
	ws = fft_ctx->window_size;

	for (uint32_t i = 0; i < ch; i++) {
		if (fft_ctx->fft_buffers[i]) {
			av_rdft_calc(fft_ctx->rdft_context,
				fft_ctx->fft_buffers[i]);
		}
	}

	__m128 wsize = _mm_set_ps1((float)ws * 0.5f);
	for (uint32_t i = 0; i < ch; i++) {
		float *fbuffer = fft_ctx->fft_buffers[i];
		for (uint32_t j = 0; j < ws; j += 4 ) {
			_mm_store_ps(fbuffer + j,
				_mm_div_ps(_mm_load_ps(fbuffer +j), wsize));
		}
	}
}


static void avis_memset32(uint32_t *pixels, uint32_t val, size_t size)
{
	uint32_t *addr = pixels;
	for (uint32_t *d = addr; d < addr + size; d++)
		*(d) = val;
}

static void draw_vis(void *data)
{
	struct audiovis_source *context = data;

	uint32_t w, h;

	if (!context)
		return;

	uint8_t *pixels = context->framebuffer;

	if (!pixels || !context->tex || !context->fft_context)
		return;

	avis_copy_buffers_and_apply_window(context);
	avis_process_fft(context);

	w = context->cx;
	h = context->cy;
	
#if 0

	avis_memset32((uint32_t *)pixels, context->bg_color, w * h);

#endif // 0


#if 1
	float cs = 1.0f - 10.0f * context->frame_time;

	for (uint32_t i = 0; i < h; i++) {
		for (uint32_t j = 0; j < w; j++) {
			uint8_t *addr = (pixels + (i * w + j) * 4);
			*addr = (uint8_t)(*addr * cs);
			*(addr + 1) = (uint8_t)(*(addr + 1) * cs);
			*(addr + 2) = (uint8_t)(*(addr + 2) * cs);
			*(addr + 3) = (uint8_t)(*(addr + 3) * cs);
		}
	}



	float speed = 3;
	int rows = (int)(100.0f * context->frame_time * speed);
	if (rows)
		memmove(pixels + rows * w * 4, pixels + w * 4,
			(h - rows) * w * 4);
#endif // 0


	uint32_t bins     = context->bins;
	uint32_t *bi      = context->bins_indexes;
	uint32_t channels = context->fft_context->channels;
	
	for (uint32_t b = 0; b < bins; b++) {
		uint32_t start = bi[b] + 1;
		uint32_t stop  = bi[b + 1] + 1;
		float mag = 0;
		for (uint32_t nch = 0; nch < channels; nch++) {
			float *buffer = context->fft_context->fft_buffers[nch];
			float bmag = 0;

			if (!buffer)
				break;

			for (uint32_t o = start; o < stop; o++) {
				float re, im;
				re = buffer[o * 2];
				im = buffer[o * 2 + 1];
				bmag += re * re + im * im;
			}
			mag += bmag / (stop -start);
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
				context->frame_time * 10;
		}
		else {
			context->spectrum[b] = mag;
		}
		if (context->spec_peaks[b] > mag) {
			context->spec_peaks[b] +=
				log10f(context->spec_peaks[b]) *
				context->frame_time / 1.5f;
			if (context->spec_peaks[b] < context->spectrum[b]) {
				context->spec_peaks[b] = context->spectrum[b];
			}
		}
		else {
			context->spec_peaks[b] = mag;
		}
	}
	
	uint32_t bands = bins - 1;


	int fbw = w / bands;
	int pad = (w - bands * fbw) / 2;
	int bpad = (int)((float)fbw * 0.2f);
	int bw = fbw - bpad;
	avis_rect_t view = { 0, 0, w, h };

	for (uint32_t b = 0; b < bands; b++) {
		int bh = (int)(h * context->spectrum[b]);
		int by = (int)h - bh;
		int ph = (int)(h * context->spec_peaks[b]);
		int py = (int)h - ph;
		avis_rect_t bar = {pad + fbw * b, by, bw, bh};
		avis_rect_t peak = { pad + fbw * b, py - 1, bw, 2 };

		avis_draw_bar((uint32_t *)pixels, &bar, &view,
			context->fg_color);
#if 0
		avis_draw_bar((uint32_t *)pixels, &peak, &view,
			0xFF000000);  
#endif // 0

	}

	obs_enter_graphics();
	gs_texture_set_image(context->tex, context->framebuffer, w * 4, false);
	obs_leave_graphics();

}

static void audiovis_source_render(void *data, gs_effect_t *effect)
{
	struct audiovis_source *context = data;

	if (!data)
		return;

	if (!context->tex)
		return;
	if (!context->can_render)
		return;

	gs_reset_blend_state();

	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"),
			context->tex);
	gs_draw_sprite(context->tex, 0, context->cx, context->cy);
}

static void audiovis_source_tick(void *data, float seconds)
{
	struct audiovis_source *context = data;

	context->global_time += seconds;
	context->frame_time = seconds;
	
	if (!context->audio_source) {
		context->acq_retry_timeout -= seconds;
		if (context->acq_retry_timeout < 0.0f) {
			avis_acquire_audio_source(context);
			context->acq_retry_timeout = ACQ_RETRY_TIMEOUT_S;
		}
	}
	else if (context->can_render && context->visible) {
		draw_vis(context);
	}
}

struct enum_sources_proc_params
{
	obs_property_t *prop;
	struct audiovis_source *context;
};


bool enum_sources_proc(void *param, obs_source_t *source)
{
	struct enum_sources_proc_params *params = param;

	struct audiovis_source *context = params->context;
	obs_property_t		  *prop = params->prop;
	
	const char *name = obs_source_get_name(source);

	uint32_t flags   = obs_source_get_output_flags(source);

	if (flags & OBS_SOURCE_AUDIO)
		obs_property_list_add_string(prop, name, name);

	return true;
}

static obs_properties_t *audiovis_source_properties(void *data)
{
	struct audiovis_source *context = data;
	obs_properties_t *props;
	obs_property_t   *prop;

	props = obs_properties_create();
	prop = obs_properties_add_list(
		props,
		SETTINGS_AUDIO_SOURCES,
		TEXT_AUDIO_SOURCES_DESC,
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_STRING);

	struct enum_sources_proc_params params;
	params.prop = prop;
	params.context = context;

	for (uint32_t i = 1; i <= 10; i++) {
		obs_source_t *source = obs_get_output_source(i);
		if (source) {
			enum_sources_proc(&params, source);
			obs_source_release(source);
		}
	}

	obs_enum_sources(enum_sources_proc, &params);

	prop = obs_properties_add_list(
		props,
		SETTINGS_FFT_SIZE,
		TEXT_FFT_SIZE_DESC,
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	struct dstr str = { 0 };
	int ws[] = {1024, 2048, 4096};

	for (int i = 0; i < AVIS_ARRAY_LEN(ws); i++) {
		dstr_printf(&str, "%d", ws[i]);
		obs_property_list_add_int(prop, str.array, ws[i]);
	}

	prop = obs_properties_add_list(
		props,
		SETTINGS_OCTAVE_FRACT,
		TEXT_OCTAVE_FRACT_DESC,
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	int ofd[] = { 1, 3, 6, 12};
	
	for (int i = 0; i < AVIS_ARRAY_LEN(ofd); i++) {
		dstr_printf(&str, "1 / %d", ofd[i]);
		obs_property_list_add_int(prop, str.array, ofd[i]);
	}

	dstr_free(&str);

	const char *wgtype[] = {"Z (Flat)", "A"};

	prop = obs_properties_add_list(
		props,
		SETTINGS_WGHT_TYPE,
		TEXT_WGHT_TYPE_DESC,
		OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);

	for (int i = 0; i < 2; i++)
		obs_property_list_add_int(prop, wgtype[i], i);

	obs_properties_add_color(props, SETTINGS_FG_COLOR, TEXT_FG_COLOR_DESC);
	obs_properties_add_color(props, SETTINGS_BG_COLOR, TEXT_BG_COLOR_DESC);

	return props;
}

static void audiovis_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, SETTINGS_AUDIO_SOURCES, "");
	obs_data_set_default_int(settings, SETTINGS_FFT_SIZE, 4096);
	obs_data_set_default_int(settings, SETTINGS_OCTAVE_FRACT, 3);
	obs_data_set_default_int(settings, SETTINGS_FG_COLOR, 0xFF00FFFF);
	obs_data_set_default_int(settings, SETTINGS_BG_COLOR, 0x00000000);
	obs_data_set_default_int(settings, SETTINGS_WGHT_TYPE, 1);
}

static struct obs_source_info audiovis_source_info = {
	.id             = "audiovis_source",
	.type           = OBS_SOURCE_TYPE_INPUT,
	.output_flags   = OBS_SOURCE_VIDEO,
	.get_name       = audiovis_source_get_name,
	.create         = audiovis_source_create,
	.destroy        = audiovis_source_destroy,
	.update         = audiovis_source_update,
	.get_defaults   = audiovis_source_defaults,
	.show           = audiovis_source_show,
	.hide           = audiovis_source_hide,
	.activate       = audiovis_source_activate,
	.deactivate     = audiovis_source_deactivate,
	.get_width      = audiovis_source_getwidth,
	.get_height     = audiovis_source_getheight,
	.video_tick     = audiovis_source_tick,
	.video_render   = audiovis_source_render,
	.get_properties = audiovis_source_properties
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("audiovis-source", "en-US")

bool obs_module_load(void)
{
	obs_register_source(&audiovis_source_info);
	return true;
}

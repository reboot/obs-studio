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

#include "audio-monitor.h"

audio_monitor_t * audio_monitor_init(const char *name,
	uint32_t sample_rate, uint32_t channels, size_t size)
{
	audio_monitor_t *am;

	am = bzalloc(sizeof(audio_monitor_t));

	dstr_init_copy(&am->name, name);
	
	am->data = audio_buffer_create(sample_rate, channels, size);
	
	pthread_mutex_init(&am->data_mutex, NULL);

	audio_monitor_acquire_obs_source(am);

	return am;
}

void audio_monitor_destroy(audio_monitor_t *am)
{
	if (!am)
		return;

	audio_monitor_release_obs_source(am);

	dstr_free(&am->name);

	audio_buffer_destroy(am->data);
	pthread_mutex_destroy(&am->data_mutex);
	
	bfree(am);
}


static void audio_monitor_removed_signal(void *vptr, calldata_t *calldata);
static void audio_monitor_data_received_signal(void *vptr,
	calldata_t *calldata);

void audio_monitor_acquire_obs_source(audio_monitor_t *am)
{
	if (!am) return;

	if (am->source) return;

	bool global_source_found = false;

	obs_source_t *src = NULL;
	signal_handler_t *sh;

	for (uint32_t i = 1; i <= 10; i++) {
		obs_source_t *source = obs_get_output_source(i);
		if (source) {
			uint32_t flags = obs_source_get_output_flags(source);
			if (flags & OBS_SOURCE_AUDIO) {
				const char *name = obs_source_get_name(source);
				if (am->name.array) {
					if (strcmp(name, am->name.array) == 0) {
						global_source_found = true;
						src = source;
						break;
					}
				}
			}
			obs_source_release(source);
		}
	}

	if (!global_source_found && am->name.array)
		src = obs_get_source_by_name(am->name.array);

	if (!src) return;

	am->source = src;

	sh = obs_source_get_signal_handler(src);

	signal_handler_connect(sh, "audio_data",
		audio_monitor_data_received_signal, am);
	signal_handler_connect(sh, "remove",
		audio_monitor_removed_signal, am);
}

void audio_monitor_release_obs_source(audio_monitor_t *am)
{
	if (!am) return;

	if (am->source) {
		signal_handler_t *sh;

		sh = obs_source_get_signal_handler(am->source);

		pthread_mutex_lock(&am->data_mutex);

		signal_handler_disconnect(sh, "audio_data",
			audio_monitor_data_received_signal, am);
		signal_handler_disconnect(sh, "remove",
			audio_monitor_removed_signal, am);

		pthread_mutex_unlock(&am->data_mutex);

		obs_source_release(am->source);
		am->source = NULL;
	}
}


static void audio_monitor_data_received_signal(void *vptr,
	calldata_t *calldata)
{
	audio_monitor_t     *am = vptr;
	struct audio_data *data = calldata_ptr(calldata, "data");
	size_t frames, window_size, offset;
	uint32_t channels;

	if (!am) return;

	if (pthread_mutex_trylock(&am->data_mutex) == EBUSY) return;

	frames = data->frames;
	window_size = am->data->size;
	offset = window_size - frames;

	am->data->volume = data->volume;

	if (frames > window_size)
		goto BAD_REALLY_BAD;

	channels = am->data->channels;

	for (uint32_t i = 0; i < channels; i++) {
		float *abuffer = am->data->buffer[i];
		float *adata = (float*)data->data[i];
		if (adata) {
			memmove(abuffer,
				abuffer + frames,
				offset * sizeof(float));

			memcpy(abuffer + offset,
				adata,
				frames * sizeof(float));
		}
	}
BAD_REALLY_BAD:
	pthread_mutex_unlock(&am->data_mutex);
}

static void audio_monitor_removed_signal(void *vptr, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);

	audio_monitor_t *am = vptr;

	if (!am) return;

	audio_monitor_release_obs_source(am);
}

#pragma once

#include <obs-module.h>
#include <util/dstr.h>
#include <util/threading.h>

#include "audio-buffer.h"

struct audio_monitor {
	obs_source_t    *source;
	audio_buffer_t  *data;
	struct dstr     name;
	pthread_mutex_t data_mutex;
};
typedef struct audio_monitor audio_monitor_t;

audio_monitor_t * audio_monitor_init(const char *name,
	uint32_t sample_rate, uint32_t channels, size_t size);
void audio_monitor_destroy(audio_monitor_t *am);
void audio_monitor_acquire_obs_source(audio_monitor_t *am);
void audio_monitor_release_obs_source(audio_monitor_t *am);

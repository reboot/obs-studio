#pragma once

#include <obs-module.h>

struct audio_buffer{
	float      *buffer[MAX_AV_PLANES];
	size_t     size;
	uint32_t   sample_rate;
	uint32_t   channels;
	float      volume;

};

typedef struct audio_buffer audio_buffer_t;

audio_buffer_t * audio_buffer_create(uint32_t sample_rate, uint32_t channels,
	size_t size);
void audio_buffer_destroy(audio_buffer_t *ab);

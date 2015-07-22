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

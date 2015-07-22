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
#include "audio-buffer.h"

struct visual {
	void  *(*create)(obs_data_t *settings, uint32_t sample_rate, 
		uint32_t channels);
	void  (*destroy)(void *data);

	void  (*render)(void *data, gs_effect_t *effect);
	void  (*tick)(void *data, float seconds);
	void  (*process_audio)(void *data, audio_buffer_t *audio);

	void  (*get_defaults)(obs_data_t *settings);
	void  (*get_properties)(void *data, obs_properties_t *props);

	uint32_t (*get_width)(void *data);
	uint32_t (*get_height)(void *data);
	size_t   (*frame_size)(void *data);
};

typedef struct visual visual_t;

struct visual_info {
	void     *data;
	visual_t visual;
};

typedef struct visual_info visual_info_t;

visual_info_t * vi_create(visual_t *vis);

void vi_destroy(visual_info_t *info);

void visual_create(visual_info_t *vi, obs_data_t *settings,
	uint32_t sample_rate, uint32_t channels);

void visual_destroy(visual_info_t *vi);

void visual_render(visual_info_t *vi, gs_effect_t *effect);

void visual_tick(visual_info_t *vi, float seconds);

void visual_process_audio(visual_info_t *vi, audio_buffer_t *audio);

void visual_get_properties(visual_info_t *vi, obs_properties_t *props);

uint32_t visual_get_width(visual_info_t *vi);

uint32_t visual_get_height(visual_info_t *vi);

size_t visual_frame_size(visual_info_t *vi);

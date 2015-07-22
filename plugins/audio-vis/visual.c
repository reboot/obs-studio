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

#include "visual.h"

visual_info_t * vi_create(visual_t *vis)
{
	visual_info_t *vi;

	vi = bzalloc(sizeof(visual_info_t));

	memcpy(&vi->visual, vis, sizeof(visual_t));

	return vi;
}

void vi_destroy(visual_info_t *info)
{
	if (!info) return;
	bfree(info);
}

static inline bool visual_info_valid(visual_info_t *vi)
{
	return vi && vi->data;
}

void visual_create(visual_info_t *vi, obs_data_t *settings,
	uint32_t sample_rate, uint32_t channels)
{
	if (!vi) return;

	vi->data = vi->visual.create(settings, sample_rate, channels);
}

void visual_destroy(visual_info_t *vi)
{
	if (!vi) return;
	if (vi->data && vi->visual.destroy)
		vi->visual.destroy(vi->data);
}

void visual_render(visual_info_t *vi, gs_effect_t *effect)
{
	if (!vi) return;
	if (vi->data && vi->visual.render)
		vi->visual.render(vi->data, effect);

}

void visual_tick(visual_info_t *vi, float seconds)
{
	if (!vi) return;
	if (vi->data && vi->visual.tick)
		vi->visual.tick(vi->data, seconds);

}

void visual_process_audio(visual_info_t *vi, audio_buffer_t *audio)
{
	if (!vi) return;
	if (vi->data && vi->visual.process_audio)
		vi->visual.process_audio(vi->data, audio);
}

void visual_get_properties(visual_info_t *vi, obs_properties_t *props)
{
	if (!vi) return;
	if (vi->data && vi->visual.get_properties)
		vi->visual.get_properties(vi->data, props);
}

uint32_t visual_get_width(visual_info_t *vi)
{
	if (!visual_info_valid(vi)) return 0;
	return vi->visual.get_width(vi->data);
}

uint32_t visual_get_height(visual_info_t *vi)
{
	if (!visual_info_valid(vi)) return 0;
	return vi->visual.get_height(vi->data);
}

size_t visual_frame_size(visual_info_t *vi)
{
	if (!visual_info_valid(vi)) return 0;
	return vi->visual.frame_size(vi->data);
}

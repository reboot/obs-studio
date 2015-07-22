#include "audio-buffer.h"

audio_buffer_t * audio_buffer_create(uint32_t sample_rate, uint32_t channels,
	size_t size)
{
	audio_buffer_t *ab;
	uint32_t ch;

	ab = bzalloc(sizeof(audio_buffer_t));

	ab->sample_rate = sample_rate;
	ab->channels    = channels;
	ab->size        = size;

	for (ch = 0; ch < channels; ch++)
		ab->buffer[ch] = bzalloc(size * sizeof(float));

	return ab;
}

void audio_buffer_destroy(audio_buffer_t *ab)
{
	uint32_t ch;

	if (!ab) return;

	for (ch = 0; ch < ab->channels; ch++)
		bfree(ab->buffer[ch]);

	bfree(ab);
}

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start playback subsystem (returns 0 on success)
int audio_playback_start();
// Stop playback subsystem
void audio_playback_stop();
// Enqueue PCM samples for playback (samples = number of int16 samples)
void audio_playback_enqueue(const int16_t* pcm, size_t samples, int sampleRate, int channels);

#ifdef __cplusplus
}
#endif
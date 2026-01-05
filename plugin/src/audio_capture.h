#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pcm_callback_t)(const int16_t* pcm, size_t samples, int sampleRate, int channels);

// Start capture (returns 0 on success)
int audio_capture_start();
// Stop capture
void audio_capture_stop();
// Register a callback that will be called for every captured PCM buffer
void audio_capture_set_callback(pcm_callback_t cb);

#ifdef __cplusplus
}
#endif
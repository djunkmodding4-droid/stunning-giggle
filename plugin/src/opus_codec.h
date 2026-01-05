#pragma once
#include <stdint.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Opus encoder (sampleRate, channels), returns 0 on success
int opus_encoder_init(int sampleRate, int channels);
// Encodes a PCM frame (int16) and returns a vector with encoded bytes (caller must free)
std::vector<unsigned char> opus_encode_frame(const int16_t* pcm, size_t samples);
// Destroy encoder and free resources
void opus_encoder_destroy();

#ifdef __cplusplus
}
#endif
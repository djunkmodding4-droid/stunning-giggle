#pragma once
#include <stdint.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Opus decoder (sampleRate, channels), returns 0 on success
int opus_decoder_init(int sampleRate, int channels);
// Decode an Opus packet and return PCM samples (int16) - returned vector contains interleaved samples
std::vector<int16_t> opus_decode_packet(const unsigned char* data, size_t len);
// Destroy decoder
void opus_decoder_destroy();

#ifdef __cplusplus
}
#endif
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Minimal C API for the WebRTC bridge (native plugin)
int StartWebRTC();
void StopWebRTC();

int StartAudioCapture();
void StopAudioCapture();

void CreateOfferForPlayer(int32_t playerId);
void HandleSignalFromPlayer(int32_t playerId, const char* json);

// Enqueue an incoming Opus packet for playout buffering
void enqueue_incoming_opus_packet(int32_t playerId, const unsigned char* data, size_t len);

#ifdef __cplusplus
}
#endif

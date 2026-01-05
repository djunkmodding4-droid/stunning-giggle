#pragma once

#include <stdint.h>

#ifdef _WIN32
  #define EXPORT __declspec(dllexport)
#else
  #define EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Start the plugin's background server/bridge (call from engine/plugin init)
EXPORT void StartPlugin();

// Stop the plugin (call on shutdown)
EXPORT void StopPlugin();

// Called by the game mod to set a global mute for a player (host authoritative)
EXPORT void SetGlobalMute(int32_t playerId, int mute);

// Return a JSON string representing current players and transforms (caller owns pointer)
// For simplicity in the skeleton we return a pointer to an internal string (read-only)
EXPORT const char* GetPlayersJson();

// Allow the engine (or an external helper) to set the players JSON (useful if plugin cannot access engine directly)
// JSON should be: { "players": [ { "id": 1, "name": "Alex", "pos": [x,y,z], "yaw": 0, "pitch": 0 }, ... ] }
EXPORT void SetPlayersJson(const char* json);

// Set local client proximity settings (range in meters and volume multiplier)
EXPORT void SetLocalProximitySettings(float rangeMeters, float volume);
// Set local mute for a remote player (client-level preference)
EXPORT void SetLocalMute(int32_t playerId, int mute);

// Get attenuation multiplier for a source player (0.0 - 1.0) based on proximity and mutes
EXPORT float GetAttenuationForPlayer(int32_t sourcePlayerId);

// Returns the local player's id (or -1 if unknown)
EXPORT int GetLocalPlayerId();
// Returns 1 if player is globally muted, 0 otherwise
EXPORT int IsPlayerGloballyMuted(int32_t playerId);
// Start the native WebRTC subsystem (allocates threads, ICE stack, etc.). Returns 0 on success.
EXPORT int StartWebRTC();
// Stop the native WebRTC subsystem and clean up resources.
EXPORT void StopWebRTC();

// Start/Stop local audio capture (mic). Returns 0 on success.
EXPORT int StartAudioCapture();
EXPORT void StopAudioCapture();

// Create an SDP offer for a remote player (playerId is the in-game player identifier).
EXPORT void CreateOfferForPlayer(int32_t playerId);

// Handle a signaling message (SDP or ICE candidate) from a remote player. `json` is a small JSON object
// such as { "type": "sdp", "sdp": "...", "sdpType": "offer|answer" } or { "type": "candidate", "candidate": "..." }.
EXPORT void HandleSignalFromPlayer(int32_t playerId, const char* json);

// Retrieve the most recent local SDP (offer/answer) generated for a player. Caller must not free the pointer.
EXPORT const char* GetLocalSdpForPlayer(int32_t playerId);

// Blocking pop/wait for next event. Waits up to timeoutMs milliseconds and returns a pointer to a string event or "" on timeout.
// Event strings are in the format:
//  - local-sdp|<playerId>|<base64_sdp>|<sdpType>
//  - candidate|<playerId>|<base64_candidate>
// Caller must not free the returned pointer; pointer remains valid until next call.
EXPORT const char* PluginWaitForEvent(int timeoutMs);

// Non-blocking pop, returns "" if none available
EXPORT const char* PluginPopEvent();

// Fast non-blocking check for queued events (returns 1 if events available, 0 otherwise)
EXPORT int PluginHasEvent();

// Register a Lua handler name to be invoked by the engine when events occur (shim; engine support required)
// Example: PluginRegisterEventHandler("onPluginEvent")
EXPORT void PluginRegisterEventHandler(const char* handlerName);
EXPORT const char* PluginGetRegisteredEventHandler();
EXPORT int PluginHasRegisteredEventHandler();

// Debug controls: set/get flag to enable verbose plugin logging (1=on, 0=off)
EXPORT void PluginSetDebug(int debug);
EXPORT int PluginGetDebug();

// Feed an incoming Opus packet (received from network/peer) to the plugin for decoding and playback
// `data` points to the Opus packet bytes and `len` is the packet length
EXPORT void HandleIncomingOpusPacket(int32_t playerId, const unsigned char* data, size_t len);

#ifdef __cplusplus
}
#endif

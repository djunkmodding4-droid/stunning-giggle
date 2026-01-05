#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Initialize peer manager (optional config can be added later)
void peer_manager_init();
void peer_manager_shutdown();

// Create an offer-oriented PeerConnection for playerId. Returns 0 on success.
int peer_manager_create_offer_for_player(int32_t playerId);

// Apply signaling JSON from remote player to our peer connection
int peer_manager_handle_signal(int32_t playerId, const std::string& json);

// Forcefully disconnect and remove the peer connection for playerId (used for global-mute enforcement)
void peer_manager_force_disconnect(int32_t playerId);

// Periodic tick for peer manager to perform deferred work and cleanup. Safe to call from a background thread.
void peer_manager_tick();

// Send an Opus packet to all connected peers (or to a specific player later)
void peer_manager_send_opus_to_all(const std::vector<uint8_t>& packet);

// Get last local SDP (offer) for a player, returns empty string if none
std::string peer_manager_get_local_sdp(int32_t playerId);

// Set the expected next local SDP type for a peer ("offer" or "answer")
void peer_manager_set_expected_sdp_type(int32_t playerId, const std::string& t);

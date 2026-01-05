#include "webrtc_peer_manager.h"
#include "plugin_debug.h"
#include <iostream>
#include <map>
#include <memory>
#include <mutex>

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std::chrono_literals;

struct Peer {
  int32_t playerId;
  std::unique_ptr<rtc::PeerConnection> pc;
  std::shared_ptr<rtc::DataChannel> dc;
  std::string lastLocalSdp;
};

static std::mutex g_peers_mutex;
static std::map<int32_t, std::shared_ptr<Peer>> g_peers;
static rtc::Configuration g_rtc_cfg;

void peer_manager_init() {
  // Add a public STUN server by default for NAT traversal
  g_rtc_cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");
}

void peer_manager_shutdown() {
  std::lock_guard<std::mutex> lk(g_peers_mutex);
  for (auto &kv : g_peers) {
    if (kv.second->pc) kv.second->pc->close();
  }
  g_peers.clear();
}

int peer_manager_create_offer_for_player(int32_t playerId) {
  std::lock_guard<std::mutex> lk(g_peers_mutex);
  // Do not create offers for players that are globally muted
  extern int IsPlayerGloballyMuted(int32_t);
  if (IsPlayerGloballyMuted(playerId)) {
    std::cerr << "peer_manager: refusing to create offer for globally muted player " << playerId << std::endl;
    extern void push_event_string(const std::string&);
    std::string ev = std::string("blocked|") + std::to_string(playerId);
    push_event_string(ev);
    return -1;
  }

  if (g_peers.find(playerId) != g_peers.end()) {
    std::cerr << "peer_manager: peer already exists for player " << playerId << std::endl;
    return -1;
  }

  auto p = std::make_shared<Peer>();
  p->playerId = playerId;
  try {
    p->pc = std::make_unique<rtc::PeerConnection>(g_rtc_cfg);
  } catch (const std::exception &e) {
    std::cerr << "peer_manager: failed to create PeerConnection: " << e.what() << std::endl;
    return -1;
  }

  // Create an audio data channel (binary)
  p->dc = p->pc->createDataChannel("audio");

  // When the datachannel receives binary messages (Opus packets), forward them to the plugin decoder
  p->dc->onMessage([playerId](std::variant<rtc::binary, rtc::string> message) {
    if (std::holds_alternative<rtc::binary>(message)) {
      auto bin = std::get<rtc::binary>(message);
      // Enqueue packet for playout buffering
      extern void enqueue_incoming_opus_packet(int32_t, const unsigned char*, size_t);
      if (!bin.empty()) {
        enqueue_incoming_opus_packet(playerId, bin.data(), bin.size());
      }
    }
  });

  // Notify Lua/plugin about datachannel open/close so UI can show status
  p->dc->onOpen([playerId]() {
    extern void push_event_string(const std::string&);
    std::string ev = std::string("peer-open|") + std::to_string(playerId);
    push_event_string(ev);
  });
  p->dc->onClose([playerId]() {
    extern void push_event_string(const std::string&);
    std::string ev = std::string("peer-closed|") + std::to_string(playerId);
    push_event_string(ev);
  });

  // Store local SDP when generated
  p->expectedLocalSdpType = "offer"; // default for createOffer()
  p->pc->onLocalDescription([p](rtc::Description desc) {
    p->lastLocalSdp = std::string(desc);
    if (g_plugin_debug.load()) std::cout << "peer_manager: local SDP for player " << p->playerId << " generated (len=" << p->lastLocalSdp.size() << ")" << std::endl;
    // push an event for Lua to process and relay: format -> local-sdp|playerId|<base64(sdp)>|<sdpType>
    extern void push_event_string(const std::string&);
    // base64 encode
    auto sdp = p->lastLocalSdp;
    static const char* b="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    int val=0, valb=-6;
    for (unsigned char c : sdp) {
      val = (val<<8) + c;
      valb += 8;
      while (valb>=0) {
        enc.push_back(b[(val>>valb)&0x3F]);
        valb-=6;
      }
    }
    if (valb>-6) enc.push_back(b[((val<<8)>>(valb+8))&0x3F]);
    while (enc.size()%4) enc.push_back('=');
    std::string ev = std::string("local-sdp|") + std::to_string(p->playerId) + std::string("|") + enc + std::string("|") + p->expectedLocalSdpType;
    push_event_string(ev);
  });

  // Log local ICE candidates
  p->pc->onLocalCandidate([playerId](rtc::Candidate c) {
    if (g_plugin_debug.load()) std::cout << "peer_manager: local candidate for player " << playerId << " -> " << c.candidate() << std::endl;
    extern void push_event_string(const std::string&);
    std::string cand = c.candidate();
    // base64 encode candidate string
    static const char* b="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    int val=0, valb=-6;
    for (unsigned char ch : cand) {
      val = (val<<8) + ch;
      valb += 8;
      while (valb>=0) {
        enc.push_back(b[(val>>valb)&0x3F]);
        valb-=6;
      }
    }
    if (valb>-6) enc.push_back(b[((val<<8)>>(valb+8))&0x3F]);
    while (enc.size()%4) enc.push_back('=');
    std::string ev = std::string("candidate|") + std::to_string(playerId) + std::string("|") + enc;
    push_event_string(ev);
    // Optionally we could store candidates and expose them via an API
  });

  // Keep the peer
  g_peers[playerId] = p;

  // Create an offer (this will trigger onLocalDescription)
  p->pc->createOffer();
  return 0;
}

int peer_manager_handle_signal(int32_t playerId, const std::string& jsonStr) {
  std::lock_guard<std::mutex> lk(g_peers_mutex);
  json obj;
  try { obj = json::parse(jsonStr); } catch(...) { return -1; }

  if (!obj.contains("type")) return -1;
  std::string type = obj["type"].get<std::string>();
  if (type == "sdp") {
    if (!obj.contains("sdp")) return -1;
    std::string sdp = obj["sdp"].get<std::string>();
    // Ensure peer exists (create if remote offers first)
    if (g_peers.find(playerId) == g_peers.end()) {
      // Create a Peer but do not create our own offer; we'll respond with answer
      auto p = std::make_shared<Peer>();
      p->playerId = playerId;
      p->pc = std::make_unique<rtc::PeerConnection>(g_rtc_cfg);
      p->dc = p->pc->createDataChannel("audio");
      p->dc->onMessage([playerId](std::variant<rtc::binary, rtc::string> message) {
        if (std::holds_alternative<rtc::binary>(message)) {
          auto bin = std::get<rtc::binary>(message);
          extern void enqueue_incoming_opus_packet(int32_t, const unsigned char*, size_t);
          if (!bin.empty()) enqueue_incoming_opus_packet(playerId, bin.data(), bin.size());
        }
      });
      p->pc->onLocalDescription([p](rtc::Description desc) {
        p->lastLocalSdp = std::string(desc);
        if (g_plugin_debug.load()) std::cout << "peer_manager: local SDP for player " << p->playerId << " generated (len=" << p->lastLocalSdp.size() << ")" << std::endl;
      });
      g_peers[playerId] = p;
    }
    auto p = g_peers[playerId];
    // set remote description
    p->pc->setRemoteDescription(rtc::Description(sdp));
      // If the remote offered, create an answer
    if (obj.contains("sdpType") && obj["sdpType"].get<std::string>() == "offer") {
      p->expectedLocalSdpType = "answer";
      p->pc->createAnswer();
    }
    return 0;
  } else if (type == "candidate") {
    if (!obj.contains("candidate")) return -1;
    std::string cand = obj["candidate"].get<std::string>();
    if (g_peers.find(playerId) == g_peers.end()) return -1;
    g_peers[playerId]->pc->addRemoteCandidate(rtc::Candidate(cand));
    return 0;
  }
  return -1;
}

void peer_manager_send_opus_to_all(const std::vector<uint8_t>& packet) {
  std::lock_guard<std::mutex> lk(g_peers_mutex);
  for (auto &kv : g_peers) {
    auto &p = kv.second;
    if (p && p->dc && p->dc->isOpen()) {
      p->dc->send(rtc::binary(packet));
    }
  }
}

std::string peer_manager_get_local_sdp(int32_t playerId) {
  std::lock_guard<std::mutex> lk(g_peers_mutex);
  if (g_peers.find(playerId) == g_peers.end()) return std::string();
  return g_peers[playerId]->lastLocalSdp;
}

void peer_manager_force_disconnect(int32_t playerId) {
  std::lock_guard<std::mutex> lk(g_peers_mutex);
  auto it = g_peers.find(playerId);
  if (it == g_peers.end()) return;
  try {
    if (it->second->pc) it->second->pc->close();
  } catch(...) {}
  g_peers.erase(it);
  // push a peer-closed event so UI updates immediately
  extern void push_event_string(const std::string&);
  std::string ev = std::string("peer-closed|") + std::to_string(playerId);
  push_event_string(ev);
}

// Periodic tick called from the webrtc background thread. Currently performs lightweight maintenance.
void peer_manager_tick() {
  // Future work: prune stale peers, run connection health checks, or collect metrics.
  // For now this is intentionally lightweight and safe to call frequently.
  std::lock_guard<std::mutex> lk(g_peers_mutex);
  // (no-op)
}

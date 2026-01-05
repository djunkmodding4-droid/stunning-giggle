#include "webrtc_bridge.h"
#include "plugin_api.h"
#include "plugin_debug.h"
#include "audio_playback.h"

#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <vector>

static std::atomic<bool> g_webrtc_running(false);
static std::thread g_webrtc_thread;

EXPORT int StartWebRTC() {
  if (g_webrtc_running.load()) return 0;
  g_webrtc_running.store(true);

  // initialize decoder and playback so we are ready to receive packets
  if (opus_decoder_init(48000, 1) != 0) {
    std::cerr << "webrtc: failed to init opus decoder" << std::endl;
  }
  if (audio_playback_start() != 0) {
    std::cerr << "webrtc: failed to start audio playback" << std::endl;
  }

  // start playout thread
  g_playout_running.store(true);
  g_playout_thread = std::thread(playout_thread_func);

  g_webrtc_thread = std::thread([](){
    if (g_plugin_debug.load()) std::cout << "webrtc: background thread started" << std::endl;
    while (g_webrtc_running.load()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      // perform periodic maintenance in the peer manager (no-op for now)
      extern void peer_manager_tick();
      peer_manager_tick();
    }
    if (g_plugin_debug.load()) std::cout << "webrtc: background thread exiting" << std::endl;
  });
  return 0;
}

EXPORT void StopWebRTC() {
  if (!g_webrtc_running.load()) return;
  g_webrtc_running.store(false);
  if (g_webrtc_thread.joinable()) g_webrtc_thread.join();
  // stop playout thread
  g_playout_running.store(false);
  g_playout_cv.notify_all();
  if (g_playout_thread.joinable()) g_playout_thread.join();

  // stop audio playback and destroy decoder
  audio_playback_stop();
  opus_decoder_destroy();
}

#include "audio_capture.h"
#include "opus_codec.h"
#include "opus_decoder.h"
#include "webrtc_peer_manager.h"

#include <deque>
#include <map>
#include <condition_variable>

static void pcm_received_cb(const int16_t* pcm, size_t samples, int sampleRate, int channels) {
  // If our local player was globally muted by the host, avoid sending audio
  extern int GetLocalPlayerId();
  extern int IsPlayerGloballyMuted(int32_t);
  int localId = GetLocalPlayerId();
  if (localId >= 0 && IsPlayerGloballyMuted(localId)) {
    // Do not encode/send audio when globally muted
    return;
  }

  // basic: feed pcm into opus encoder and print the encoded packet size
  auto packet = opus_encode_frame(pcm, samples);
  if (!packet.empty()) {
    if (g_plugin_debug.load()) std::cout << "webrtc: encoded opus packet size=" << packet.size() << " bytes" << std::endl;
    // send packet to all connected peers over datachannel
    std::vector<uint8_t> v(packet.begin(), packet.end());
    peer_manager_send_opus_to_all(v);
  }
}

// Playout buffering structures
static std::mutex g_playout_mutex;
static std::condition_variable g_playout_cv;
static std::map<int32_t, std::deque<std::pair<std::vector<uint8_t>, std::chrono::steady_clock::time_point>>> g_playout_queues;
static std::atomic<bool> g_playout_running(false);
static std::thread g_playout_thread;

static void playout_thread_func() {
  const int MIN_PACKETS = 2;
  const auto MAX_WAIT = std::chrono::milliseconds(100);
  while (g_playout_running.load()) {
    std::unique_lock<std::mutex> lk(g_playout_mutex);
    g_playout_cv.wait_for(lk, std::chrono::milliseconds(20));
    if (!g_playout_running.load()) break;
    // process queues
    for (auto &kv : g_playout_queues) {
      auto &dq = kv.second;
      if (dq.empty()) continue;
      auto now = std::chrono::steady_clock::now();
      auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - dq.front().second);
      if ((int)dq.size() >= MIN_PACKETS || age >= MAX_WAIT) {
        auto pkt = std::move(dq.front().first);
        dq.pop_front();
        lk.unlock();
        // decode
        auto pcm = opus_decode_packet(pkt.data(), pkt.size());
        if (!pcm.empty()) {
          extern float GetAttenuationForPlayer(int32_t);
          float att = 1.0f;
          try { att = GetAttenuationForPlayer(kv.first); } catch(...) { att = 1.0f; }
          if (att > 0.0f) {
            if (att < 0.999f) {
              for (size_t i = 0; i < pcm.size(); ++i) {
                int v = (int)std::lround((float)pcm[i] * att);
                if (v > 32767) v = 32767; if (v < -32768) v = -32768;
                pcm[i] = (int16_t)v;
              }
            }
            // compute short-term level (RMS) and emit event (throttled)
            double sum = 0.0;
            for (size_t i = 0; i < pcm.size(); ++i) sum += ((double)pcm[i] * (double)pcm[i]);
            double rms = 0.0;
            if (!pcm.empty()) rms = sqrt(sum / (double)pcm.size()) / 32768.0;
            int level = (int)std::round(std::min(1.0, rms) * 100.0);
            extern void push_event_string(const std::string&);
            static std::map<int32_t, std::chrono::steady_clock::time_point> g_last_level_sent;
            auto now = std::chrono::steady_clock::now();
            auto it = g_last_level_sent.find(kv.first);
            bool send = false;
            if (it == g_last_level_sent.end()) send = true; else {
              auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
              if (diff.count() >= 150) send = true;
            }
            if (send) {
              g_last_level_sent[kv.first] = now;
              std::string ev = std::string("level|") + std::to_string(kv.first) + std::string("|") + std::to_string(level);
              push_event_string(ev);
            }

            audio_playback_enqueue(pcm.data(), pcm.size(), 48000, 1);
          }
        }
        lk.lock();
      }
    }
  }
}

void enqueue_incoming_opus_packet(int32_t playerId, const unsigned char* data, size_t len) {
  if (!data || len == 0) return;
  std::lock_guard<std::mutex> lk(g_playout_mutex);
  auto &dq = g_playout_queues[playerId];
  std::vector<uint8_t> v(data, data + len);
  dq.emplace_back(std::move(v), std::chrono::steady_clock::now());
  g_playout_cv.notify_one();
}

EXPORT int StartAudioCapture() {
  if (g_plugin_debug.load()) std::cout << "webrtc: StartAudioCapture" << std::endl;
  if (opus_encoder_init(48000, 1) != 0) {
    std::cerr << "webrtc: failed to init opus encoder" << std::endl;
    return -1;
  }
  audio_capture_set_callback(pcm_received_cb);
  if (audio_capture_start() != 0) {
    std::cerr << "webrtc: failed to start audio capture" << std::endl;
    return -1;
  }
  return 0;
}

EXPORT void StopAudioCapture() {
  if (g_plugin_debug.load()) std::cout << "webrtc: StopAudioCapture" << std::endl;
  audio_capture_stop();
  opus_encoder_destroy();
}

EXPORT void CreateOfferForPlayer(int32_t playerId) {
  if (g_plugin_debug.load()) std::cout << "webrtc: CreateOfferForPlayer(" << playerId << ")" << std::endl;
  peer_manager_create_offer_for_player(playerId);
}

EXPORT void HandleSignalFromPlayer(int32_t playerId, const char* json) {
  if (g_plugin_debug.load()) std::cout << "webrtc: HandleSignalFromPlayer(" << playerId << ") payload=" << (json?json:"(null)") << std::endl;
  if (!json) return;
  try {
    peer_manager_handle_signal(playerId, std::string(json));
  } catch (...) {
    std::cerr << "webrtc: failed to handle signal" << std::endl;
  }
}

EXPORT const char* GetLocalSdpForPlayer(int32_t playerId) {
  static std::string s;
  s = peer_manager_get_local_sdp(playerId);
  return s.c_str();
}

EXPORT void HandleIncomingOpusPacket(int32_t playerId, const unsigned char* data, size_t len) {
  // For compatibility, forward to playout queue
  enqueue_incoming_opus_packet(playerId, data, len);
}
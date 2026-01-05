#include "plugin_api.h"
#include "plugin_debug.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <map>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cmath>

static std::atomic<bool> g_running{false};
static std::thread g_thread;
static std::mutex g_lock;
static std::map<int, bool> g_globalMuted; // playerId -> muted
static std::string g_playersJson = "{ \"players\": [] }";
static std::map<int, std::array<double,3>> g_playerPositions; // playerId -> {x,y,z}
static int g_localPlayerId = -1;
static float g_localRangeMeters = 20.0f;
static float g_localVolume = 1.0f;
static std::map<int, bool> g_localMuted; // local preferences

// This implementation uses cpp-httplib (header-only) to expose simple endpoints:
// GET /mutes   -> { "globalMuted": { "1": true, "2": false } }
// GET /players -> returns string from GetPlayersJson()
// POST /mute   -> { "playerId": <id>, "mute": true }

#include <httplib.h>
#include <nlohmann/json.hpp>
#include "event_queue.h"
using json = nlohmann::json;

static std::unique_ptr<httplib::Server> g_server;
static std::atomic<long long> g_lastUpdatedMs{0};
static std::condition_variable g_cv;

static long long nowMs() {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static void serverLoop()
{
    try {
        g_server = std::make_unique<httplib::Server>();

        g_server->Get("/mutes", [](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lk(g_lock);
            json out;
            out["globalMuted"] = json::object();
            for (auto &kv : g_globalMuted) {
                out["globalMuted"][std::to_string(kv.first)] = true;
            }
            out["lastUpdated"] = g_lastUpdatedMs.load();
            res.set_content(out.dump(), "application/json");
        });

        g_server->Get("/players", [](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lk(g_lock);
            // g_playersJson is already JSON string
            res.set_content(g_playersJson, "application/json");
        });

        // Long-polling events endpoint: GET /events?since=<ms>
        // Returns immediately if lastUpdated > since, otherwise waits up to 15s.
        g_server->Get("/events", [](const httplib::Request& req, httplib::Response& res) {
            long long since = 0;
            if (req.has_param("since")) {
                try { since = std::stoll(req.get_param_value("since")); } catch(...) { since = 0; }
            }

            std::unique_lock<std::mutex> lk(g_lock);
            if (g_lastUpdatedMs.load() <= since) {
                // wait up to 15 seconds or until updated
                g_cv.wait_for(lk, std::chrono::seconds(15), [&]{ return g_lastUpdatedMs.load() > since; });
            }

            json out;
            out["lastUpdated"] = g_lastUpdatedMs.load();
            out["globalMuted"] = json::object();
            for (auto &kv : g_globalMuted) {
                out["globalMuted"][std::to_string(kv.first)] = true;
            }
            res.set_content(out.dump(), "application/json");
        });

        g_server->Post("/mute", [](const httplib::Request& req, httplib::Response& res) {
            try {
                auto j = json::parse(req.body);
                if (j.contains("playerId") && j.contains("mute")) {
                    int pid = j["playerId"].get<int>();
                    bool mute = j["mute"].get<bool>();
                    SetGlobalMute(pid, mute ? 1 : 0);
                    json out; out["ok"] = true;
                    res.set_content(out.dump(), "application/json");
                    return;
                }
            } catch (...) {}
            res.status = 400;
            json out; out["ok"] = false; out["error"] = "invalid payload";
            res.set_content(out.dump(), "application/json");
        });

        // Listen only on localhost for security
        if (!g_server->listen("127.0.0.1", 8765)) {
            std::cerr << "ekz_vc_plugin: failed to bind HTTP server on 127.0.0.1:8765" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "ekz_vc_plugin server error: " << e.what() << std::endl;
    }
}


// Note: StartPlugin launches serverLoop in thread; StopPlugin will call stop() below in addition to joining.
extern "C" {

EXPORT void StartPlugin()
{
    if (g_running.load()) return;
    g_running.store(true);
    g_thread = std::thread(serverLoop);
    if (g_plugin_debug.load()) std::cout << "ekz_vc_plugin: started plugin HTTP server thread\n";
}

EXPORT void StopPlugin()
{
    if (!g_running.load()) return;
    g_running.store(false);
    if (g_server) {
        g_server->stop();
    }
    if (g_thread.joinable()) g_thread.join();
    g_server.reset();
    if (g_plugin_debug.load()) std::cout << "ekz_vc_plugin: stopped plugin\n";
}


EXPORT void SetGlobalMute(int32_t playerId, int mute)
{
    {
        std::lock_guard<std::mutex> lk(g_lock);
        if (mute) g_globalMuted[(int)playerId] = true; else g_globalMuted.erase((int)playerId);
        g_lastUpdatedMs.store(nowMs());
    }
    g_cv.notify_all();
    if (g_plugin_debug.load()) std::cout << "ekz_vc_plugin: SetGlobalMute(" << playerId << ", " << mute << ") updated at " << g_lastUpdatedMs.load() << "\n";
    // push an event for Lua/plugin consumers so clients can react immediately
    try {
        std::string ev = std::string("global-mute|") + std::to_string(playerId) + std::string("|") + (mute ? "1" : "0");
        push_event_string(ev);
    } catch(...) {}

    // Force-disconnect any local PeerConnections for this player to immediately stop audio
    if (mute) {
        try {
            extern void peer_manager_force_disconnect(int32_t);
            peer_manager_force_disconnect(playerId);
        } catch(...) {}
    }
}

EXPORT const char* GetPlayersJson()
{
    std::lock_guard<std::mutex> lk(g_lock);
    return g_playersJson.c_str();
}

EXPORT void SetPlayersJson(const char* json)
{
    if (!json) return;
    {
        std::lock_guard<std::mutex> lk(g_lock);
        g_playersJson = std::string(json);
        // parse positions if possible
        try {
            auto j = json::parse(g_playersJson);
            g_playerPositions.clear();
            if (j.contains("players") && j["players"].is_array()) {
                for (auto &p : j["players"]) {
                    if (p.contains("id") && p.contains("pos") && p["pos"].is_array() && p["pos"].size() >= 3) {
                        int id = p["id"].get<int>();
                        double x = p["pos"][0].get<double>();
                        double y = p["pos"][1].get<double>();
                        double z = p["pos"][2].get<double>();
                        g_playerPositions[id] = {x,y,z};
                    }
                }
            }
            if (j.contains("localId")) {
                try { g_localPlayerId = j["localId"].get<int>(); } catch(...) { g_localPlayerId = -1; }
            }
        } catch (...) {
            // ignore parse errors
        }
        g_lastUpdatedMs.store(nowMs());
    }
    g_cv.notify_all();
    if (g_plugin_debug.load()) std::cout << "ekz_vc_plugin: SetPlayersJson called, updated at " << g_lastUpdatedMs.load() << "\n";
}

EXPORT void SetLocalProximitySettings(float rangeMeters, float volume) {
    std::lock_guard<std::mutex> lk(g_lock);
    if (rangeMeters > 0) g_localRangeMeters = rangeMeters;
    if (volume >= 0) g_localVolume = volume;
    if (g_plugin_debug.load()) std::cout << "ekz_vc_plugin: SetLocalProximitySettings(range=" << g_localRangeMeters << ", vol=" << g_localVolume << ")" << std::endl;
}

EXPORT void SetLocalMute(int32_t playerId, int mute) {
    std::lock_guard<std::mutex> lk(g_lock);
    if (mute) g_localMuted[playerId] = true; else g_localMuted.erase(playerId);
    if (g_plugin_debug.load()) std::cout << "ekz_vc_plugin: SetLocalMute(" << playerId << ", " << mute << ")" << std::endl;
}

EXPORT float GetAttenuationForPlayer(int32_t sourcePlayerId) {
    std::lock_guard<std::mutex> lk(g_lock);
    // global mute wins
    if (g_globalMuted.find(sourcePlayerId) != g_globalMuted.end()) return 0.0f;
    if (g_localMuted.find(sourcePlayerId) != g_localMuted.end()) return 0.0f;
    if (g_localPlayerId < 0) return g_localVolume; // no position info
    auto itSrc = g_playerPositions.find(sourcePlayerId);
    auto itMe = g_playerPositions.find(g_localPlayerId);
    if (itSrc == g_playerPositions.end() || itMe == g_playerPositions.end()) return g_localVolume;
    double dx = itSrc->second[0] - itMe->second[0];
    double dy = itSrc->second[1] - itMe->second[1];
    double dz = itSrc->second[2] - itMe->second[2];
    double dist = sqrt(dx*dx + dy*dy + dz*dz);
    if (dist >= g_localRangeMeters) return 0.0f;
    double att = 1.0 - (dist / g_localRangeMeters); // linear falloff
    return (float)(att * g_localVolume);
}

EXPORT int GetLocalPlayerId() {
    std::lock_guard<std::mutex> lk(g_lock);
    return g_localPlayerId;
}

EXPORT int IsPlayerGloballyMuted(int32_t playerId) {
    std::lock_guard<std::mutex> lk(g_lock);
    return (g_globalMuted.find((int)playerId) != g_globalMuted.end()) ? 1 : 0;
}

}

// Implementation notes:
// - Embedded HTTP endpoints (/players, /mutes, /events, /mute) are implemented and served on 127.0.0.1:8765 by default.
// - The plugin exposes C API functions that can be bound by the engine's plugin system. Engine-dependent registration
//   and callbacks for direct Lua invocation remain an integration step for the game engine.

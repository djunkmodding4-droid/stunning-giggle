#include "event_queue.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

static std::queue<std::string> g_q;
static std::mutex g_m;
static std::condition_variable g_cv;

void push_event_string(const std::string& s) {
  {
    std::lock_guard<std::mutex> lk(g_m);
    g_q.push(s);
  }
  g_cv.notify_one();
}

std::string wait_pop_event(int timeoutMs) {
  std::unique_lock<std::mutex> lk(g_m);
  if (g_q.empty()) {
    if (timeoutMs <= 0) return std::string();
    auto ok = g_cv.wait_for(lk, std::chrono::milliseconds(timeoutMs));
    if (!ok) return std::string();
  }
  if (g_q.empty()) return std::string();
  auto s = g_q.front();
  g_q.pop();
  return s;
}

std::string pop_event_nonblocking() {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_q.empty()) return std::string();
  auto s = g_q.front();
  g_q.pop();
  return s;
}

bool has_event() {
  std::lock_guard<std::mutex> lk(g_m);
  return !g_q.empty();
} 
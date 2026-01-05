#include "event_queue.h"
#include "plugin_api.h"
#include <string>

// Return buffer pointer
static std::string g_return_buf;

EXPORT const char* PluginWaitForEvent(int timeoutMs) {
  auto s = wait_pop_event(timeoutMs);
  g_return_buf = s;
  return g_return_buf.c_str();
}

EXPORT const char* PluginPopEvent() {
  auto s = pop_event_nonblocking();
  g_return_buf = s;
  return g_return_buf.c_str();
}

// Fast non-blocking check for queued events (returns 1 if events available, 0 otherwise)
EXPORT int PluginHasEvent() {
  return has_event() ? 1 : 0;
}

// Event handler registration shim. Store the name of a Lua handler if the engine supports calling it.
static std::string g_registered_handler;

EXPORT void PluginRegisterEventHandler(const char* handlerName) {
  if (handlerName) g_registered_handler = handlerName; else g_registered_handler.clear();
}

EXPORT const char* PluginGetRegisteredEventHandler() {
  g_return_buf = g_registered_handler;
  return g_return_buf.c_str();
}

#include "plugin_debug.h"
#include <atomic>

std::atomic<int> g_plugin_debug(0);

EXPORT int PluginHasRegisteredEventHandler() {
  return g_registered_handler.empty() ? 0 : 1;
}

// Set/Get plugin debug mode (1=on, 0=off)
EXPORT void PluginSetDebug(int debug) {
  g_plugin_debug.store(debug ? 1 : 0);
}

EXPORT int PluginGetDebug() {
  return g_plugin_debug.load();
} 

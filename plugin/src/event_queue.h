#pragma once

#include <string>

// Push a raw event string into the queue (thread-safe)
void push_event_string(const std::string& s);

// Wait up to timeoutMs milliseconds and pop an event string. Returns empty string on timeout.
std::string wait_pop_event(int timeoutMs);

// Non-blocking pop, returns empty string if none available
std::string pop_event_nonblocking();

// Return true if there is at least one event queued
bool has_event();
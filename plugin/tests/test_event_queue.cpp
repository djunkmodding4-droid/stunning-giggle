#include "event_queue.h"
#include <iostream>
#include <cassert>

int main() {
  // Ensure queue is empty initially
  if (has_event()) {
    std::cerr << "FAIL: queue not empty at start" << std::endl;
    return 2;
  }

  // Push event
  push_event_string("test|1|payload");

  if (!has_event()) {
    std::cerr << "FAIL: has_event returned false after push" << std::endl;
    return 3;
  }

  std::string ev = pop_event_nonblocking();
  if (ev != "test|1|payload") {
    std::cerr << "FAIL: popped event mismatch: " << ev << std::endl;
    return 4;
  }

  if (has_event()) {
    std::cerr << "FAIL: queue not empty after pop" << std::endl;
    return 5;
  }

  // Test wait_pop_event with timeout=0 returns immediately
  std::string ev2 = wait_pop_event(0);
  if (!ev2.empty()) {
    std::cerr << "FAIL: wait_pop_event returned non-empty on empty queue" << std::endl;
    return 6;
  }

  std::cout << "PASS" << std::endl;
  return 0;
}

#include "event_api.h"
#include <iostream>

int main() {
  // Register handler
  PluginRegisterEventHandler("onPluginEventTest");
  if (!PluginHasRegisteredEventHandler()) {
    std::cerr << "FAIL: PluginHasRegisteredEventHandler returned false" << std::endl;
    return 2;
  }
  const char* name = PluginGetRegisteredEventHandler();
  if (!name || std::string(name) != "onPluginEventTest") {
    std::cerr << "FAIL: handler name mismatch: " << (name?name:"(null)") << std::endl;
    return 3;
  }
  // Clear handler
  PluginRegisterEventHandler(nullptr);
  if (PluginHasRegisteredEventHandler()) {
    std::cerr << "FAIL: Handler not cleared" << std::endl;
    return 4;
  }
  std::cout << "PASS" << std::endl;
  return 0;
}

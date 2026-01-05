#include "plugin_api.h"
#include <iostream>

int main() {
  // Basic set/get test for PluginSetDebug / PluginGetDebug
  PluginSetDebug(1);
  if (!PluginGetDebug()) {
    std::cerr << "FAIL: PluginGetDebug did not return 1 after set(1)" << std::endl;
    return 2;
  }
  PluginSetDebug(0);
  if (PluginGetDebug()) {
    std::cerr << "FAIL: PluginGetDebug did not return 0 after set(0)" << std::endl;
    return 3;
  }
  std::cout << "PASS" << std::endl;
  return 0;
}
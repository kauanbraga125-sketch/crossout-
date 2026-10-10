#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// Native PS4 path access: transplanted from Trophy Unlocker 13.52 v13.18.
// Returns 0 or a negative POSIX errno, never fabricates an empty directory.
int usbfix_native_list_directory(const std::string& path,std::vector<std::string>& names);

struct UsbfixGoldhenAccess {
  int64_t sdk,access;
  uint64_t sdk_cf,access_cf;
  bool attempted,access_attempted;
  UsbfixGoldhenAccess():sdk(0),access(0),sdk_cf(0),access_cf(0),
                         attempted(false),access_attempted(false){}
  bool acknowledged()const{return access_attempted && access==0;}
};
// Explicit user action only; NEVER invoked automatically at startup.
UsbfixGoldhenAccess usbfix_request_goldhen_access();

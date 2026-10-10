#ifndef USBFIX_CORE_H
#define USBFIX_CORE_H
#include <stddef.h>
#include <stdio.h>
#include <string>
enum FixState {
 FIX_ABSENT=0, FIX_OK, FIX_CANDIDATE, FIX_LOCAL,
 FIX_MISSING, FIX_DUPLICATE, FIX_FOREIGN, FIX_DENIED
};
struct FixLink {
 FixState status;
 std::string internal,old_target,new_target;
 int errno_value, matches;
 FixLink():status(FIX_ABSENT),errno_value(0),matches(0){}
};
enum FixType { FIX_GAME=0, FIX_UPDATE=1, FIX_DLC=2 };
bool fix_valid_id(const std::string& id);
const char* fix_status(FixState status);
FixLink fix_inspect(const std::string& root,const std::string& id,FixType type);
bool fix_repair(const std::string& root,const std::string& id,FixType type,
                std::string* outcome,FILE* log);
#endif

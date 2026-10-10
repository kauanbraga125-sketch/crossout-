#pragma once
#include <string>
#include <stdint.h>
// PS4-native link operations. All functions return 0 on success or a positive
// FreeBSD errno. The readlink/symlink syscalls are only allowed after an explicit
// GoldHEN access request is acknowledged by the console user.
namespace usbfix_io {
void set_authorized(bool value);
bool authorized();
int read_link(const std::string& path,std::string* target);
int regular_file(const std::string& path,uint64_t* size);
int make_dir(const std::string& path);
int create_exclusive_backup(const std::string& path,const std::string& content);
int create_link(const std::string& target,const std::string& path);
int rename_file(const std::string& from,const std::string& to);
int remove_file(const std::string& path);
}

#include "native_catalog.h"
#include <orbis/libkernel.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <vector>
#include <string>

namespace {
static int ps4_error(int64_t status) {
  // See TrophyUnlocker1352 release/v13.18-final-stable v13/native_fs.cpp
  const uint32_t code=(uint32_t)status;
  if((code&0xffff0000u)==0x80020000u)return -(int)(code&0xffffu);
  return status<0?(int)status:0;
}
static int read_entries(const uint8_t* b,size_t bytes,std::vector<std::string>& out){
  size_t at=0;
  while(at<bytes){
    if(bytes-at<9)return -EIO;
    uint16_t rec;
    memcpy(&rec,b+at+4,2);
    uint8_t len=b[at+7];
    if(rec<9 || rec>bytes-at || len==0 || (size_t)len+9>rec)return -EIO;
    const char* name=(const char*)(b+at+8);
    if(name[len]!='\0'||memchr(name,0,len)||memchr(name,'/',len))return -EIO;
    if(!(len==1&&name[0]=='.')){
      if(!((len==2)&&name[0]=='.'&&name[1]=='.')){
        if(out.size()>=8192)return -EOVERFLOW;
        out.push_back(std::string(name,len));
      }
    }
    at+=rec;
  }
  return 0;
}
struct GoldhenReply {int64_t value;uint64_t carry;};
struct JailbreakBackup {
 uint32_t uid,ruid,rgid,groups;
 uint64_t paid,caps[2];
 void *prison,*cdir,*jdir,*rdir;
};
static_assert(sizeof(JailbreakBackup)==72,"GoldHEN backup ABI size");
static_assert(offsetof(JailbreakBackup,prison)==40,"GoldHEN prison ABI offset");
}
int usbfix_native_list_directory(const std::string& path,
                                  std::vector<std::string>& names){
 names.clear();
 if(path.empty()||path[0]!='/'||path.size()>768)return -EINVAL;
 int fd=sceKernelOpen(path.c_str(),O_RDONLY|O_DIRECTORY,0);
 if(fd<0)return ps4_error(fd);
 uint8_t bytes[16384];
 int status=0;
 for(;;){
   int n=sceKernelGetdents(fd,(char*)bytes,sizeof(bytes));
   if(ps4_error(n)==-EINTR)continue;
   if(n<0){status=ps4_error(n);break;}
   if(n==0)break;
   if(n>(int)sizeof(bytes)){status=-EIO;break;}
   status=read_entries(bytes,(size_t)n,names);
   if(status)break;
 }
 int closed=ps4_error(sceKernelClose(fd));
 if(!status)status=closed;
 if(status)names.clear();
 return status;
}
extern "C" GoldhenReply usbfix_goldhen_command(uint64_t command,void* data);
asm(".text\n"
    ".global usbfix_goldhen_command\n"
    "usbfix_goldhen_command:\n"
    "mov %rsi, %rdx\n"
    "mov %rdi, %rsi\n"
    "mov $500, %rdi\n"
    "xor %eax, %eax\n"
    "mov %rcx, %r10\n"
    "syscall\n"
    "setc %dl\n"
    "movzbl %dl, %edx\n"
    "ret\n");
UsbfixGoldhenAccess usbfix_request_goldhen_access(){
 static UsbfixGoldhenAccess result;
 static JailbreakBackup backup={};
 if(result.acknowledged())return result;
 result=UsbfixGoldhenAccess();
 result.attempted=true;
 GoldhenReply ver=usbfix_goldhen_command(0,nullptr);
 result.sdk=ver.value;
 result.sdk_cf=ver.carry;
 // Trophy Unlocker v13.18 only sends access command when SDK version 0x100.
 if(ver.value!=0x100)return result;
 memset(&backup,0,sizeof(backup));
 result.access_attempted=true;
 GoldhenReply ret=usbfix_goldhen_command(2,&backup);
 result.access=ret.value;
 result.access_cf=ret.carry;
 return result;
}

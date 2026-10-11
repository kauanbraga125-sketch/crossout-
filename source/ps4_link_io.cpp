#include "ps4_link_io.h"
#include <orbis/libkernel.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace {
bool g_authorized=false;
const char* g_backup_step="not-started";
static int errcode(int64_t value) {
  uint32_t bits=(uint32_t)value;
  if((bits&0xffff0000u)==0x80020000u)return (int)(bits&0xffffu);
  if(value<0 && value>=-4095)return (int)-value;
  return value<0?EIO:0;
}
struct RawResponse { int64_t value; uint64_t carry; };
// PS4 BSD ABI: RAX=call number, RDI/RSI/RDX=arguments and CF=error.
// Used ONLY for readlink(58)/symlink(57), which OpenOrbis musl stubs as ENOSYS.
// The same calling convention is used by the working Trophy Unlocker GoldHEN
// request; these two syscall numbers are not tested on the user's 13.52 yet.
extern "C" RawResponse usbfix_ps4_bsd3(uint64_t n,
    const void* arg0,const void* arg1,uint64_t arg2);
asm(".text\n"
    ".global usbfix_ps4_bsd3\n"
    "usbfix_ps4_bsd3:\n"
    "mov %rdi,%rax\n"
    "mov %rsi,%rdi\n"
    "mov %rdx,%rsi\n"
    "mov %rcx,%rdx\n"
    "syscall\n"
    "setc %dl\n"
    "movzbl %dl,%edx\n"
    "ret\n");
static int raw_error(RawResponse r) {
 if(!r.carry)return 0;
 uint64_t n=(uint64_t)r.value;
 return n>0 && n<4096?(int)n:EIO;
}
static int close_status(int fd){
 return errcode(sceKernelClose(fd));
}
}
namespace usbfix_io {
void set_authorized(bool value){g_authorized=value;}
bool authorized(){return g_authorized;}
int read_link(const std::string& path,std::string* target){
 if(target)target->clear();
 if(!g_authorized)return EAGAIN;
 if(path.empty()||path[0]!='/')return EINVAL;
 char value[1024];
 RawResponse resp=usbfix_ps4_bsd3(58,path.c_str(),value,sizeof(value)-1);
 int failure=raw_error(resp);
 if(failure)return failure;
 if(resp.value<=0||resp.value>=(int64_t)sizeof(value)-1)return ENAMETOOLONG;
 value[resp.value]=0;
 if(target)*target=value;
 return 0;
}
int regular_file(const std::string& path,uint64_t* size){
 if(size)*size=0;
 if(path.empty()||path[0]!='/')return EINVAL;
 int fd=sceKernelOpen(path.c_str(),O_RDONLY,0);
 if(fd<0)return errcode(fd);
 // IMPORTANT: PS4 native stat layout is different from the SDK's struct stat!
 // Offsets verified by orbis-compat on console: mode=0x08, size=0x48.
 alignas(16) unsigned char raw[256]={};
 int rc=sceKernelFstat(fd,reinterpret_cast<OrbisKernelStat*>(raw));
 int er=errcode(rc);
 const int close_error=close_status(fd);
 if(er)return er;
 if(close_error)return close_error;
 uint16_t mode=0;
 int64_t length=0;
 memcpy(&mode,raw+0x08,sizeof(mode));
 memcpy(&length,raw+0x48,sizeof(length));
 if((mode&S_IFMT)!=S_IFREG)return EINVAL;
 if(length<1024)return EINVAL;
 if(size)*size=(uint64_t)length;
 return 0;
}
int make_dir(const std::string& path){
 int rc=sceKernelMkdir(path.c_str(),0700);
 int e=errcode(rc);
 return e==EEXIST?0:e;
}
const char* backup_error_step(){return g_backup_step;}
int create_exclusive_backup(const std::string& path,const std::string& content){
 g_backup_step="open";
 int fd=sceKernelOpen(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
 if(fd<0)return errcode(fd);
 int e=0;
 size_t at=0;
 g_backup_step="write";
 while(at<content.size()){
   int64_t n=(int64_t)sceKernelWrite(fd,content.data()+at,content.size()-at);
   if(n<0){e=errcode(n);break;}
   if(n==0 || (size_t)n>content.size()-at){e=EIO;break;}
   at+=(size_t)n;
 }
 if(!e){g_backup_step="fsync";e=errcode(sceKernelFsync(fd));}
 g_backup_step=e?g_backup_step:"close";
 const int closeError=close_status(fd);
 if(!e)e=closeError;
 // No link can be changed until the saved bytes have been read back.
 if(e)return e; // Preserve incomplete file so it cannot be overwritten.
 g_backup_step="verify-open";
 fd=sceKernelOpen(path.c_str(),O_RDONLY,0);
 if(fd<0)return errcode(fd);
 char buffer[512];
 if(content.size()>=sizeof(buffer)){sceKernelClose(fd);return EFBIG;}
 g_backup_step="verify-read";
 size_t got=0;
 while(got<content.size()){
   int64_t n=(int64_t)sceKernelRead(fd,buffer+got,content.size()-got);
   if(n<0){e=errcode(n);break;}
   if(n==0 || (size_t)n>content.size()-got){e=EIO;break;}
   got+=(size_t)n;
 }
 if(!e && memcmp(buffer,content.data(),content.size()))e=EIO;
 if(!e){
   char extra=0;
   int64_t n=(int64_t)sceKernelRead(fd,&extra,1);
   if(n!=0)e=(n<0?errcode(n):EIO);
 }
 g_backup_step=e?g_backup_step:"verify-close";
 int finalClose=close_status(fd);
 if(!e)e=finalClose;
 if(!e)g_backup_step="verified";
 return e;
}

int create_link(const std::string& target,const std::string& path){
 if(!g_authorized)return EAGAIN;
 if(target.empty() || target[0]!='/' || path.empty() || path[0]!='/')return EINVAL;
 RawResponse r=usbfix_ps4_bsd3(57,target.c_str(),path.c_str(),0);
 return raw_error(r);
}
int rename_file(const std::string& from,const std::string& to){
 return errcode(sceKernelRename(from.c_str(),to.c_str()));
}
int remove_file(const std::string& path){
 return errcode(sceKernelUnlink(path.c_str()));
}
}

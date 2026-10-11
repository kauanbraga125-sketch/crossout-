#include "../source/fixer.h"
#include "../source/ps4_link_io.h"
#include <map>
#include <set>
#include <string>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>

static int failures=0, passes=0, changes=0;
static bool allow=false, fail_backup=false, fail_create=false, fail_rename=false;
static bool fail_internal_data=false, fail_usb_backups=false;
static std::map<std::string,std::string> links,backups;
static std::set<std::string> files;
static void check(bool yes,const char* what){
 printf("%s native-backend: %s\n",yes?"PASS":"FAIL",what);
 if(yes)passes++;else failures++;
}
static std::string base(const std::string& id){return "/user/app/"+id+"/app.pkg";}
static std::string disk(int num,const std::string& id){
 return "/mnt/usb"+std::to_string(num)+"/itemzflow/apps/"+id+"/app.pkg";
}
static void reset(){allow=false;fail_backup=false;fail_create=false;fail_rename=false;
 fail_internal_data=false;fail_usb_backups=false;
 links.clear();backups.clear();files.clear();changes=0;}
namespace usbfix_io{
void set_authorized(bool value){allow=value;}
bool authorized(){return allow;}
int read_link(const std::string& path,std::string* target){
 if(target)target->clear();
 if(!allow)return EAGAIN;
 auto i=links.find(path);
 if(i!=links.end()){if(target)*target=i->second;return 0;}
 if(files.find(path)!=files.end())return EINVAL;
 return ENOENT;
}
int regular_file(const std::string& path,uint64_t* size){
 if(size)*size=0;
 std::string p=path;
 for(int i=0;i<3;i++){
  auto l=links.find(p);if(l==links.end())break;
  p=l->second;
 }
 // The mock only normalizes Itemzflow double slashes for path availability.
 while(p.find("//")!=std::string::npos)p.replace(p.find("//"),2,"/");
 if(files.count(p)){if(size)*size=8192;return 0;}
 return ENOENT;
}
int make_dir(const std::string& path){
 if(fail_backup)return EACCES;
 if(fail_internal_data && path.find("/data/")==0)return EACCES;
 if(fail_usb_backups && path.find("/mnt/usb1/USBLinkFixerBackups")!=std::string::npos)return EACCES;
 return 0;
}
const char* backup_error_step(){return "open";}
int create_exclusive_backup(const std::string& path,const std::string& s){
 if(fail_backup)return EACCES;
 if(fail_internal_data && path.find("/data/")==0)return EACCES;
 if(fail_usb_backups && path.find("/mnt/usb1/USBLinkFixerBackups")!=std::string::npos)return EACCES;
 if(backups.count(path))return EEXIST;
 backups[path]=s;return 0;
}
int create_link(const std::string& target,const std::string& path){
 if(!allow)return EAGAIN;
 if(fail_create)return ENOSYS;
 if(links.count(path)||files.count(path))return EEXIST;
 links[path]=target;changes++;return 0;
}
int rename_file(const std::string& src,const std::string& dst){
 if(fail_rename)return EACCES;
 auto i=links.find(src);
 if(i==links.end())return ENOENT;
 if(!links.count(dst))return EINVAL;
 links[dst]=i->second;links.erase(i);changes++;return 0;
}
int remove_file(const std::string& path){links.erase(path);return 0;}
}
int main(){
 const std::string id="CUSA12345",in=base(id);
 reset();
 links[in]=disk(0,id);
 files.insert(disk(0,id));
 check(fix_inspect("",id,FIX_GAME).status==FIX_DENIED,"no raw link syscall before user authorization");
 usbfix_io::set_authorized(true);
 check(fix_inspect("",id,FIX_GAME).status==FIX_OK,"working usb0 stays valid");
 std::string result;
 check(!fix_repair("",id,FIX_GAME,&result,nullptr)&&backups.empty(),
       "working usb0 never modified or backed up");
 reset();usbfix_io::set_authorized(true);
 links[in]="/mnt/usb0//itemzflow/apps/"+id+"/app.pkg";
 files.insert(disk(1,id));
 FixLink x=fix_inspect("",id,FIX_GAME);
 check(x.status==FIX_CANDIDATE&&x.new_target==disk(1,id),
       "broken usb0 link identifies unique existing usb1 pkg");
 check(fix_repair("",id,FIX_GAME,&result,nullptr),"repairs unique usb1 candidate");
 check(links[in]==disk(1,id),"new symlink points to usb1");
 check(!backups.empty()&&backups.begin()->second.find("/mnt/usb0//")!=std::string::npos,
       "stores exact original link in exclusive backup");
 check(files.count(disk(1,id))==1&&files.size()==1,
       "does not move or overwrite source pkg");
 check(!fix_repair("",id,FIX_GAME,&result,nullptr),"repeat repair blocked as link OK");
 reset();usbfix_io::set_authorized(true);
 links[in]=disk(3,id);files.insert(disk(0,id));files.insert(disk(1,id));
 check(fix_inspect("",id,FIX_GAME).status==FIX_DUPLICATE,"multiple USB copies ambiguous");
 check(!fix_repair("",id,FIX_GAME,&result,nullptr)&&links[in]==disk(3,id),
       "ambiguous sources never modified");
 reset();usbfix_io::set_authorized(true);
 links[in]="/arbitrary/third-party/game.pkg";files.insert(disk(1,id));
 check(fix_inspect("",id,FIX_GAME).status==FIX_FOREIGN,"unknown link never modified");
 reset();usbfix_io::set_authorized(true);
 files.insert(in);files.insert(disk(1,id));
 check(fix_inspect("",id,FIX_GAME).status==FIX_LOCAL,"local installed pkg untouched");
 reset();usbfix_io::set_authorized(true);
 links[in]=disk(0,id);
 check(fix_inspect("",id,FIX_GAME).status==FIX_MISSING,"zero candidates remain missing");
 reset();usbfix_io::set_authorized(true);
 links[in]=disk(0,id);files.insert(disk(1,id));
 fail_backup=true;
 check(!fix_repair("",id,FIX_GAME,&result,nullptr)&&links[in]==disk(0,id)&&changes==0,
       "backup permission denial blocks writes");
 fail_backup=false;fail_create=true;
 check(!fix_repair("",id,FIX_GAME,&result,nullptr)&&links[in]==disk(0,id)&&changes==0,
       "unsupported syscall 57 blocks replacement");
 fail_create=false;fail_rename=true;
 check(!fix_repair("",id,FIX_GAME,&result,nullptr)&&links[in]==disk(0,id),
       "rename failure preserves original link");
 // v1.1 regression: new backup directory is not the old, restricted one.
 reset();usbfix_io::set_authorized(true);
 links[in]=disk(0,id);files.insert(disk(1,id));
 check(fix_repair("",id,FIX_GAME,&result,nullptr),"fresh app-owned /data recovery dir permits repair");
 check(backups.size()==1 &&
       backups.begin()->first.find("/data/ULFX00001-Backups/")==0,
       "v1.1 never chooses the legacy restricted folder");
 // A USB1-local text recovery record is permitted only if /data is denied.
 reset();usbfix_io::set_authorized(true);
 links[in]=disk(0,id);files.insert(disk(1,id));fail_internal_data=true;
 check(fix_repair("",id,FIX_GAME,&result,nullptr),"fallback repair works with /data blocked but USB1 backup writable");
 check(backups.size()==1 &&
       backups.begin()->first.find("/mnt/usb1/USBLinkFixerBackups/")==0,
       "fallback writes backup only on target USB1, never USB0");
 // If NO backup location is writable, prohibit ALL link mutations.
 reset();usbfix_io::set_authorized(true);
 links[in]=disk(0,id);files.insert(disk(1,id));
 fail_internal_data=true;fail_usb_backups=true;
 check(!fix_repair("",id,FIX_GAME,&result,nullptr) && links[in]==disk(0,id) && changes==0,
       "no writable /data or USB1 backup means no mutation");
 // v1.2: FPKGi is a PS4 app with a normal 9-character title ID,
 // but it does NOT use the CUSA game prefix. It must be treated safely.
 const std::string appid="FPKGI0001",appLink=base(appid);
 check(fix_valid_id(appid),"accept homebrew title ID FPKGI0001");
 check(fix_valid_id("APOLLO001"),"accept other valid homebrew title ID");
 check(!fix_valid_id("FPKG/0001"),"block slash injection for homebrew IDs");
 check(!fix_valid_id("fpkgi0001"),"reject lowercase/inconsistent IDs");
 reset();usbfix_io::set_authorized(true);
 links[appLink]=disk(0,appid);files.insert(disk(1,appid));
 check(fix_inspect("",appid,FIX_GAME).status==FIX_CANDIDATE,
       "discover broken FPKGi symlink on USB1");
 check(fix_repair("",appid,FIX_GAME,&result,nullptr),
       "repair uniquely matched FPKGi app safely with backup");
 check(links[appLink]==disk(1,appid) && backups.size()==1,
       "homebrew repair verifies USB1 link and creates recovery record");
 reset();usbfix_io::set_authorized(true);
 links[appLink]=disk(0,appid);files.insert(disk(0,appid));
 check(fix_inspect("",appid,FIX_GAME).status==FIX_OK &&
       !fix_repair("",appid,FIX_GAME,&result,nullptr)&&changes==0,
       "never modify functioning homebrew on USB0");
 reset();usbfix_io::set_authorized(true);
 files.insert(appLink);
 check(fix_inspect("",appid,FIX_GAME).status==FIX_LOCAL,
       "installed internal homebrew app stays local and untouched");
 printf("PS4 BACKEND RESULT: %d passes, %d failures\n",passes,failures);
 return failures?1:0;
}

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
int make_dir(const std::string&){return fail_backup?EACCES:0;}
int create_exclusive_backup(const std::string& path,const std::string& s){
 if(fail_backup)return EACCES;
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
 printf("PS4 BACKEND RESULT: %d passes, %d failures\n",passes,failures);
 return failures?1:0;
}

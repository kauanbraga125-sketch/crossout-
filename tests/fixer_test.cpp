#include "../source/fixer.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string>

static int passes=0,failures=0;
static void expect(bool ok,const char* what){
 printf("%s %s\n",ok?"PASS":"FAIL",what);
 if(ok)passes++;else failures++;
}
static void dir(const std::string& s){if(mkdir(s.c_str(),0777)&&errno!=EEXIST){perror(s.c_str());exit(1);}}
static void fake_pkg(const std::string& path){
 int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
 if(fd<0){perror(path.c_str());exit(1);}
 if(ftruncate(fd,8192)<0){perror("ftruncate");exit(1);}
 close(fd);
}
static void make_usb(const std::string& root,const char* device,const char* id,
                     const char* file){
 dir(root+"/mnt/"+device);
 dir(root+"/mnt/"+device+"/itemzflow");
 dir(root+"/mnt/"+device+"/itemzflow/apps");
 dir(root+"/mnt/"+device+"/itemzflow/apps/"+id);
 fake_pkg(root+"/mnt/"+device+"/itemzflow/apps/"+id+"/"+file);
}
static std::string read_target(const std::string& file){
 char data[1024];ssize_t n=readlink(file.c_str(),data,sizeof(data)-1);
 if(n<0)return "";data[n]=0;return std::string(data);
}
int main(){
 char path[]="/tmp/usbfix-fulltest-XXXXXX";
 char *p=mkdtemp(path);if(!p){perror("mkdtemp");return 1;}
 std::string root=p,id="CUSA12345",id2="CUSA67890";
 dir(root+"/user");dir(root+"/user/app");dir(root+"/user/app/"+id);
 dir(root+"/user/app/"+id2);
 dir(root+"/user/patch");dir(root+"/user/patch/"+id);
 dir(root+"/user/addcont");dir(root+"/user/addcont/"+id);
 dir(root+"/mnt");dir(root+"/data");
 make_usb(root,"usb1",id.c_str(),"app.pkg");
 std::string internal=root+"/user/app/"+id+"/app.pkg";
 std::string wrong=root+"/mnt/usb0//itemzflow/apps/"+id+"/app.pkg";
 expect(symlink(wrong.c_str(),internal.c_str())==0,"create original Itemzflow symlink");
 FixLink a=fix_inspect(root,id,FIX_GAME);
 expect(a.status==FIX_CANDIDATE,"detect broken link whose pkg exists on usb1");
 expect(a.old_target==wrong,"preserve original exact link target");
 std::string msg;
 expect(fix_repair(root,id,FIX_GAME,&msg,NULL),"repair only the symbolic link");
 expect(fix_inspect(root,id,FIX_GAME).status==FIX_OK,"repaired link points to live file");
 expect(read_target(internal).find("usb1")!=std::string::npos,"new link points to USB1");
 expect(access((root+"/data/USBLinkFixer/"+id+"-0.original.txt").c_str(),F_OK)==0,
   "recovery record exists");
 expect(!fix_repair(root,id,FIX_GAME,&msg,NULL),"do not repair same link twice");
 expect(fix_inspect(root,id2,FIX_GAME).status==FIX_ABSENT,"missing games not touched");
 // A real internal file must never be replaced.
 fake_pkg(root+"/user/app/"+id2+"/app.pkg");
 expect(fix_inspect(root,id2,FIX_GAME).status==FIX_LOCAL,"local file recognized");
 expect(!fix_repair(root,id2,FIX_GAME,&msg,NULL),"never replace local pkg");
 // Optional patch must be handled without copying content.
 make_usb(root,"usb1",id.c_str(),"app_patch.pkg");
 std::string patch=root+"/user/patch/"+id+"/patch.pkg";
 expect(symlink((root+"/mnt/usb0//itemzflow/apps/"+id+"/app_patch.pkg").c_str(),patch.c_str())==0,
        "create old patch link");
 expect(fix_inspect(root,id,FIX_UPDATE).status==FIX_CANDIDATE,"patch candidate found");
 expect(fix_repair(root,id,FIX_UPDATE,&msg,NULL),"patch repaired");
 // DLC is separate; only exact Itemzflow CUSA-style structure is repaired.
 make_usb(root,"usb1",id.c_str(),"app_dlc.pkg");
 std::string dlc=root+"/user/addcont/"+id+"/ac.pkg";
 expect(symlink((root+"/mnt/usb0/itemzflow/apps/"+id+"/app_dlc.pkg").c_str(),dlc.c_str())==0,
        "create DLC old link");
 expect(fix_repair(root,id,FIX_DLC,&msg,NULL),"DLC repaired");
 // Duplicate on USB0 and USB1 while a symlink target is missing should not guess.
 std::string second="CUSA55555";
 dir(root+"/user/app/"+second);
 make_usb(root,"usb0",second.c_str(),"app.pkg");
 make_usb(root,"usb1",second.c_str(),"app.pkg");
 std::string ambiguous=root+"/user/app/"+second+"/app.pkg";
 expect(symlink((root+"/mnt/usb2/itemzflow/apps/"+second+"/app.pkg").c_str(),ambiguous.c_str())==0,
        "make ambiguous source link");
 expect(fix_inspect(root,second,FIX_GAME).status==FIX_DUPLICATE,"reject two existing copies");
 expect(!fix_repair(root,second,FIX_GAME,&msg,NULL),"no automatic repair with two copies");
 // Foreign symlink must be ignored despite plausible external files.
 std::string foreign="CUSA33333";
 dir(root+"/user/app/"+foreign);
 make_usb(root,"usb1",foreign.c_str(),"app.pkg");
 std::string fp=root+"/user/app/"+foreign+"/app.pkg";
 expect(symlink("/not/from/itemzflow",fp.c_str())==0,"make foreign link");
 expect(fix_inspect(root,foreign,FIX_GAME).status==FIX_FOREIGN,"foreign links are untouched");
 expect(!fix_repair(root,foreign,FIX_GAME,&msg,NULL),"reject foreign link repair");
 expect(!fix_valid_id("CUSA0000/"),"reject path-injection title id");
 // Missing required backup folder prevents changes; same link stays in place.
 std::string missing="CUSA66666";
 dir(root+"/user/app/"+missing);make_usb(root,"usb1",missing.c_str(),"app.pkg");
 std::string mp=root+"/user/app/"+missing+"/app.pkg";
 std::string wrong2=root+"/mnt/usb0/itemzflow/apps/"+missing+"/app.pkg";
 symlink(wrong2.c_str(),mp.c_str());
 // Recovery paths cannot be overwritten by another run.
 dir(root+"/data/USBLinkFixer");
 fake_pkg(root+"/data/USBLinkFixer/"+missing+"-0.original.txt");
 expect(!fix_repair(root,missing,FIX_GAME,&msg,NULL),"existing backup blocks repair");
 expect(read_target(mp)==wrong2,"blocked repair did not change original");
 printf("RESULT passes=%d failures=%d\n",passes,failures);
 return failures?1:0;
}

#include "../source/catalog.h"
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <vector>
static int fails=0,passed=0;
static void check(bool ok,const char *name) {
 printf("%s: %s\n",ok?"PASS":"FAIL",name);
 if(ok)passed++;else fails++;
}
static void mk(const std::string &s) {
 if(mkdir(s.c_str(),0700) && errno!=EEXIST){perror(s.c_str());exit(1);}
}
int main(){
 char path[]="/tmp/usb-catalog-XXXXXX";
 char *tmp=mkdtemp(path);if(!tmp){perror("mkdtemp");return 1;}
 const std::string root=tmp;
 mk(root+"/user");mk(root+"/user/app");mk(root+"/user/appmeta");
 mk(root+"/mnt");mk(root+"/mnt/usb1");mk(root+"/mnt/usb1/itemzflow");mk(root+"/mnt/usb1/itemzflow/apps");
 mk(root+"/user/app/CUSA12345");mk(root+"/user/app/CUSA88888");
 mk(root+"/mnt/usb1/itemzflow/apps/CUSA12345");
 mk(root+"/mnt/usb1/itemzflow/apps/CUSA55555");
 // This file has a CUSA name; the deliberately permissive previous
 // Trophy Unlocker scan accepts it without stat(). We only list it.
 FILE* f=fopen((root+"/user/app/CUSA44444").c_str(),"wb");if(f){fputs("a",f);fclose(f);}
 std::vector<CatalogItem> games;CatalogSummary d;
 usbfix_collect_games(root,&games,&d);
 check(d.internal==3,"use previous Trophy Unlocker readdir logic (no stat filter)");
 check(d.usb[1]==2,"enumerate Itemzflow drive 1");
 check(games.size()==4,"merge catalog from internal and USB without duplicate titles");
 bool joined=false,usbOnly=false;
 for(const auto& x:games){
  if(x.id=="CUSA12345")joined=(x.sources&CAT_INTERNAL)&&(x.sources&CAT_USB)&&(x.usb_mask&2);
  if(x.id=="CUSA55555")usbOnly=!(x.sources&CAT_INTERNAL)&&(x.sources&CAT_USB)&&(x.usb_mask&2);
 }
 check(joined,"track internal game and USB link candidate together");
 check(usbOnly,"show USB-only game even if not readable via /user/app");
 // Simulate a missing/inaccessible /user/app without depending on root chmod semantics.
 std::vector<CatalogItem> list;CatalogSummary missing;
 usbfix_collect_games(root+"/missing-root",&list,&missing);
 check(missing.internal<0 && missing.internal_errno==ENOENT,
       "distinguish unreadable/missing /user/app from empty catalog");
 check(list.empty(),"invalid roots yield diagnostic, not invented games");
 printf("CATALOG RESULT: %d passed, %d failed\n",passed,fails);
 return fails!=0;
}

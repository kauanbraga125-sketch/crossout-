#include <stdio.h>
#include "catalog.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>

static bool valid_cusa(const char* s) {
 if(!s || strlen(s)!=9 || strncmp(s,"CUSA",4))return false;
 for(int i=4;i<9;i++)if(s[i]<'0'||s[i]>'9')return false;
 return true;
}
static int scan_dir(const std::string& path,unsigned source,unsigned bit,
                    std::vector<CatalogItem>* games) {
 DIR* d=opendir(path.c_str());
 if(!d)return -errno;
 int seen=0;struct dirent* ent;
 while((ent=readdir(d))!=NULL){
   if(!valid_cusa(ent->d_name))continue;
   seen++;
   bool found=false;
   for(size_t i=0;i<games->size();i++){
     if((*games)[i].id==ent->d_name){
       (*games)[i].sources|=source;
       if(source==CAT_USB)(*games)[i].usb_mask|=bit;
       found=true;break;
     }
   }
   if(!found && games->size()<512){
     CatalogItem x;x.id=ent->d_name;x.sources=source;
     if(source==CAT_USB)x.usb_mask=bit;
     games->push_back(x);
   }
 }
 closedir(d);return seen;
}
void usbfix_collect_games(const std::string& root,
                          std::vector<CatalogItem>* result,
                          CatalogSummary* info) {
 if(!result || !info)return;
 result->clear();*info=CatalogSummary();
 info->internal=scan_dir(root+"/user/app",CAT_INTERNAL,0,result);
 if(info->internal<0)info->internal_errno=-info->internal;
 info->ext0=scan_dir(root+"/mnt/ext0/user/app",CAT_EXT0,0,result);
 info->appmeta=scan_dir(root+"/user/appmeta",CAT_APPMETA,0,result);
 info->systemmeta=scan_dir(root+"/system_data/priv/appmeta",CAT_SYSTEM_META,0,result);
 for(int i=0;i<8;i++){
   char name[32];snprintf(name,sizeof(name),"/mnt/usb%d/itemzflow/apps",i);
   info->usb[i]=scan_dir(root+name,CAT_USB,1u<<i,result);
 }
}

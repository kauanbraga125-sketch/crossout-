#include "launcher.h"
#include "fixer.h"
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// OpenOrbis declares sceLncUtilLaunchApp, but its stub library may not
// expose the symbol. Itemzflow loads libSceSystemService.sprx first.
// Resolve it at runtime to avoid a PKG that fails to link or load.
typedef int32_t (*LaunchFn)(const char*,const char*[],LncAppParam*);
static LaunchFn resolve_launch() {
 const char* modules[]={
   "/system/common/lib/libSceSystemService.sprx",
   "/system/common/lib/libSceLncUtil.sprx"
 };
 const char* symbols[]={"sceLncUtilLaunchApp","sceSystemServiceLaunchApp"};
 for(unsigned i=0;i<sizeof(modules)/sizeof(modules[0]);i++){
   int h=sceKernelLoadStartModule(modules[i],0,NULL,0,NULL,NULL);
   if(h<0)continue;
   for(unsigned j=0;j<sizeof(symbols)/sizeof(symbols[0]);j++){
      void* address=NULL;
      if(sceKernelDlsym(h,symbols[j],&address)>=0 && address){
         return (LaunchFn)address;
      }
   }
 }
 return NULL;
}
int usbfix_launch_game(const std::string& title_id,std::string* diagnostic){
 if(!fix_valid_id(title_id)){if(diagnostic)*diagnostic="ID de jogo invalido";return -1;}
 int32_t userId=-1;
 int32_t result=sceUserServiceGetForegroundUser(&userId);
 if(result<0 || userId<0){
   if(diagnostic)*diagnostic="Nao foi possivel identificar o usuario do PS4";
   return result<0?result:-2;
 }
 LaunchFn launch=resolve_launch();
 if(!launch){
   if(diagnostic)*diagnostic="Falha ao localizar funcao de abertura do PS4";
   return -3;
 }
 LncAppParam param;memset(&param,0,sizeof(param));
 param.size=sizeof(param);
 param.user_id=(uint32_t)userId;
 param.LaunchAppCheck_flag=LaunchApp_SkipSystemUpdate;
 result=launch(title_id.c_str(),NULL,&param);
 if(diagnostic){
    char text[144];
    snprintf(text,sizeof(text),result<0?"Falha ao iniciar (0x%08X)":
             "Pedido de inicio enviado (0x%08X)",(uint32_t)result);
    *diagnostic=text;
 }
 return result;
}

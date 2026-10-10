#include "launcher.h"
#include "fixer.h"
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Based on the official Itemzflow Launch_App() implementation.
// The process must have required GoldHEN application privileges.
int usbfix_launch_game(const std::string& title_id,std::string* diagnostic){
 if(!fix_valid_id(title_id)){
    if(diagnostic)*diagnostic="ID de jogo invalido";return -1;
 }
 int32_t id=-1;
 int32_t e=sceUserServiceGetForegroundUser(&id);
 if(e<0||id<0){
   if(diagnostic)*diagnostic="Falha ao identificar usuario do PS4";
   return e<0?e:-2;
 }
 // Itemzflow explicitly loads the system service module before launch.
 sceKernelLoadStartModule("/system/common/lib/libSceSystemService.sprx",0,NULL,0,NULL,NULL);
 LncAppParam param;
 memset(&param,0,sizeof(param));
 param.size=sizeof(param);
 param.user_id=(uint32_t)id;
 param.app_opt=0;
 param.crash_report=0;
 param.LaunchAppCheck_flag=LaunchApp_SkipSystemUpdate;
 int32_t result=sceLncUtilLaunchApp(title_id.c_str(),NULL,&param);
 if(diagnostic){
   char buf[120];snprintf(buf,sizeof(buf),result<0?
   "Nao foi possivel abrir o jogo, codigo 0x%08X":
   "Solicitacao de abertura enviada (0x%08X)",(unsigned int)result);
   *diagnostic=buf;
 }
 return result;
}

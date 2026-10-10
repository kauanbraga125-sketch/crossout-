#include <SDL2/SDL.h>
#include <orbis/libkernel.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <vector>
#include <string>
#include <algorithm>
#include "font8x8_basic.h"

// PS4 USB Link Fixer 0.2 - read-only interactive safety preflight.
// This build DOES NOT delete, move, overwrite, reinstall or change game links.
// Refer to Itemzflow GLES2_itemzflow.cpp for the real Itemzflow file layout.

namespace {
const int W=1280, H=720;
enum State { NO_ENTRY, VALID_LINK, BROKEN_CANDIDATE, LOCAL_FILE,
             TARGET_MISSING, MULTIPLE_COPIES, FOREIGN_LINK, ACCESS_ERROR };
struct Link {
  State state=NO_ENTRY;
  std::string source, old_target, proposed;
  int copies=0;
  int err=0;
};
struct Game {
  std::string id, name;
  Link app, patch;
};
std::vector<Game> games;
std::string message="Analisando os HDs...";
int selected=0;
bool details=false;
FILE* logFile=nullptr;
int usbCount=0;
static void logline(const char* fmt,...) {
  if(!logFile)return;
  va_list ap;va_start(ap,fmt);vfprintf(logFile,fmt,ap);va_end(ap);
  fputc('\n',logFile);fflush(logFile);
}
static void notify(const char* msg){
  OrbisNotificationRequest r;memset(&r,0,sizeof(r));
  r.type=OrbisNotificationRequestType::NotificationRequest;
  r.unk3=0;r.useIconImageUri=1;r.targetId=-1;
  strcpy(r.iconUri,"cxml://psnotification/tex_icon_champions_league");
  snprintf(r.message,sizeof(r.message),"%s",msg);
  sceKernelSendNotificationRequest(0,&r,3120,0);
}
static bool valid_cusa(const char* t) {
  if(!t || strlen(t)!=9 || strncmp(t,"CUSA",4))return false;
  for(int i=4;i<9;i++)if(!isdigit((unsigned char)t[i]))return false;
  return true;
}
static std::string usb(const std::string& id,const char* type,int i){
  char p[360];snprintf(p,sizeof(p),"/mnt/usb%d/itemzflow/apps/%s/%s",i,id.c_str(),type);
  return p;
}
static bool regular_nonempty(const std::string& p){
  struct stat st;return stat(p.c_str(),&st)==0 && S_ISREG(st.st_mode) && st.st_size>0;
}
static const char* state_name(State s){
  switch(s){
    case NO_ENTRY:return "SEM REGISTRO";
    case VALID_LINK:return "LINK OK";
    case BROKEN_CANDIDATE:return "LINK QUEBRADO - CANDIDATO";
    case LOCAL_FILE:return "ARQUIVO INTERNO";
    case TARGET_MISSING:return "ARQUIVO USB NAO ENCONTRADO";
    case MULTIPLE_COPIES:return "COPIAS MULTIPLAS";
    case FOREIGN_LINK:return "LINK DESCONHECIDO";
    default:return "ERRO DE ACESSO";
  }
}
static Link inspect(const std::string& id,const char* local,const char* ext){
  Link x;char path[300];snprintf(path,sizeof(path),"/user/%s/%s/%s",
    !strcmp(local,"app.pkg")?"app":"patch",id.c_str(),local);
  x.source=path;
  struct stat lst;
  if(lstat(path,&lst)!=0){
    x.err=errno;x.state=(errno==ENOENT?NO_ENTRY:ACCESS_ERROR);return x;
  }
  if(!S_ISLNK(lst.st_mode)){x.state=LOCAL_FILE;return x;}
  char buf[512];ssize_t n=readlink(path,buf,sizeof(buf)-1);
  if(n<0 || n>=(ssize_t)sizeof(buf)-1){x.state=ACCESS_ERROR;x.err=errno;return x;}
  buf[n]=0;x.old_target=buf;
  bool recognized=false;
  for(int i=0;i<8;i++){
    std::string p=usb(id,ext,i);
    if(x.old_target==p)recognized=true;
    if(regular_nonempty(p)){x.copies++;x.proposed=p;}
  }
  if(!recognized){x.state=FOREIGN_LINK;return x;}
  if(regular_nonempty(x.old_target)){x.state=VALID_LINK;return x;}
  if(x.copies==1){x.state=BROKEN_CANDIDATE;return x;}
  x.state=x.copies>1?MULTIPLE_COPIES:TARGET_MISSING;
  return x;
}
static uint16_t u16(const unsigned char *p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t u32(const unsigned char *p){return uint32_t(u16(p))|(uint32_t(u16(p+2))<<16);}
static std::string read_title(const std::string& id){
  char p[340];
  const char* fmt[]={"/system_data/priv/appmeta/%s/param.sfo",
                     "/user/app/%s/sce_sys/param.sfo"};
  for(int pi=0;pi<2;pi++){
    snprintf(p,sizeof(p),fmt[pi],id.c_str());
    FILE* f=fopen(p,"rb");if(!f)continue;
    unsigned char buf[16384];size_t len=fread(buf,1,sizeof(buf),f);fclose(f);
    if(len<20 || memcmp(buf,"\0PSF",4))continue;
    uint32_t keybase=u32(buf+8),database=u32(buf+12),count=u32(buf+16);
    if(count>256 || keybase>=len || database>=len || 20+size_t(count)*16>len)continue;
    for(uint32_t i=0;i<count;i++){
      const unsigned char* e=buf+20+i*16;
      size_t key=size_t(keybase)+u16(e), data=size_t(database)+u32(e+12);
      uint32_t datalen=u32(e+4);
      if(key>=len || data>=len || datalen>len-data)continue;
      if(key+5>len || memcmp(buf+key,"TITLE",5))continue;
      if(key+5<len && buf[key+5]!=0)continue;
      std::string title((const char*)(buf+data),strnlen((const char*)buf+data,datalen));
      if(title.size()>60)title=title.substr(0,60);
      if(!title.empty())return title;
    }
  }
  return id;
}
static void scan(){
  games.clear();usbCount=0;selected=0;details=false;
  for(int i=0;i<8;i++){char p[32];snprintf(p,sizeof(p),"/mnt/usb%d",i);
    DIR *u=opendir(p);if(u){usbCount++;closedir(u);}}
  DIR *d=opendir("/user/app");
  if(!d){message="ERRO: nao consigo abrir /user/app";logline("%s errno=%d",message.c_str(),errno);return;}
  struct dirent *ent;
  while((ent=readdir(d))){
    if(!valid_cusa(ent->d_name))continue;
    char p[260];snprintf(p,sizeof(p),"/user/app/%s",ent->d_name);
    struct stat st;
    if(stat(p,&st)!=0 || !S_ISDIR(st.st_mode))continue;
    Game g;g.id=ent->d_name;g.name=read_title(g.id);
    g.app=inspect(g.id,"app.pkg","app.pkg");
    g.patch=inspect(g.id,"patch.pkg","app_patch.pkg");
    logline("%s | name=%s | app=%s [%s] -> [%s] | patch=%s [%s] -> [%s]",
      g.id.c_str(),g.name.c_str(),state_name(g.app.state),g.app.old_target.c_str(),
      g.app.proposed.c_str(),state_name(g.patch.state),g.patch.old_target.c_str(),
      g.patch.proposed.c_str());
    games.push_back(g);
  }
  closedir(d);
  std::sort(games.begin(),games.end(),[](const Game&a,const Game&b){return a.name<b.name;});
  int candidates=0;
  for(const auto& g:games) if(g.app.state==BROKEN_CANDIDATE || g.patch.state==BROKEN_CANDIDATE)candidates++;
  char m[220];snprintf(m,sizeof(m),"%d jogos | %d HDs | %d com links suspeitos",
    (int)games.size(),usbCount,candidates);
  message=m;logline("SUMMARY %s; READ ONLY",message.c_str());
}
static void fill(SDL_Renderer *r,int x,int y,int w,int h,SDL_Color c){
  SDL_Rect z{x,y,w,h};SDL_SetRenderDrawColor(r,c.r,c.g,c.b,255);SDL_RenderFillRect(r,&z);
}
static SDL_Color C(int r,int g,int b){SDL_Color c={(Uint8)r,(Uint8)g,(Uint8)b,255};return c;}
static void label(SDL_Renderer *r,int x,int y,const std::string& text,int zoom,SDL_Color color,size_t max=100){
  SDL_SetRenderDrawColor(r,color.r,color.g,color.b,255);
  int xx=x;
  size_t count=0;
  for(size_t k=0;k<text.size()&&count<max;k++,count++){
    unsigned char ch=(unsigned char)text[k];
    if(ch>127){ch='?';}
    if(ch<32)ch=' ';
    for(int a=0;a<8;a++){
      unsigned char row=(unsigned char)font8x8_basic[ch][a];
      for(int b=0;b<8;b++)if(row&(1u<<b)){
        SDL_Rect pixel={xx+b*zoom,y+a*zoom,zoom,zoom};SDL_RenderFillRect(r,&pixel);
      }
    }
    xx+=8*zoom;
  }
}
static SDL_Color status_color(State s){
  if(s==VALID_LINK)return C(101,226,164);
  if(s==BROKEN_CANDIDATE)return C(255,185,92);
  if(s==ACCESS_ERROR || s==FOREIGN_LINK)return C(242,114,123);
  return C(174,190,208);
}
static std::string shorten(const std::string& s,int len){
  if((int)s.size()<=len)return s;
  return s.substr(0,len-3)+"...";
}
static void draw(SDL_Renderer* r){
  fill(r,0,0,W,H,C(10,17,30));
  fill(r,0,0,W,107,C(23,47,79));
  label(r,48,35,"PS4 USB LINK FIXER",3,C(239,246,254));
  label(r,49,75,"v0.2 - DIAGNOSTICO SEGURO (SEM ALTERACOES)",1,C(194,214,237));
  label(r,50,126,message,2,C(211,222,241),66);
  if(games.empty()){
    label(r,95,310,"Nenhum jogo CUSA encontrado. Consulte o log.",2,C(234,214,180),59);
  }else{
    const int shown=12;
    int start=selected-(shown/2);
    if(start<0)start=0;
    if(start>int(games.size())-shown)start=std::max(0,int(games.size())-shown);
    for(int i=start;i<int(games.size()) && i<start+shown;i++){
      int y=184+(i-start)*36;
      const Game& g=games[i];
      if(i==selected)fill(r,35,y-7,1210,34,C(42,78,116));
      label(r,48,y,g.id,2,C(154,197,237),10);
      label(r,235,y,shorten(g.name,41),2,C(235,241,248),43);
      label(r,930,y,shorten(state_name(g.app.state),19),1,status_color(g.app.state),19);
    }
  }
  if(details&&!games.empty()){
    const Game&g=games[selected];
    fill(r,70,212,1140,320,C(28,42,61));
    fill(r,70,212,1140,6,C(81,150,218));
    label(r,94,237,shorten(g.name,45),2,C(250,251,254),46);
    label(r,94,277,g.id+"   JOGO: "+state_name(g.app.state),2,status_color(g.app.state),55);
    label(r,94,317,"ATUAL: "+shorten(g.app.old_target,74),1,C(211,223,239),100);
    label(r,94,339,"LOCALIZADO: "+shorten(g.app.proposed,74),1,C(211,223,239),100);
    label(r,94,377,"ATUALIZACAO: "+std::string(state_name(g.patch.state)),2,status_color(g.patch.state),55);
    label(r,94,417,"ATUAL: "+shorten(g.patch.old_target,74),1,C(211,223,239),100);
    label(r,94,439,"LOCALIZADO: "+shorten(g.patch.proposed,74),1,C(211,223,239),100);
    label(r,94,486,"Apenas analise. Nenhum link foi modificado.",2,C(255,189,107),65);
  }
  fill(r,0,660,W,60,C(22,36,55));
  label(r,50,678,"D-PAD: navegar    X: detalhes    TRIANGULO: reler    O: sair",2,C(207,222,237),66);
}
static void nav(int change){
  if(games.empty())return;
  selected+=change;
  if(selected<0)selected=0;
  if(selected>=int(games.size()))selected=int(games.size())-1;
  details=false;
}
static void handle(const int code,bool& running){
  if(code==1)nav(-1);
  else if(code==2)nav(1);
  else if(code==3)details=!details;
  else if(code==4){scan();}
  else if(code==5){if(details)details=false;else running=false;}
}
}
int main(){
  mkdir("/data/USBLinkFixer",0700);
  logFile=fopen("/data/USBLinkFixer/scan.txt","wb");
  if(!logFile)logFile=fopen("/data/USBLinkFixer-scan.txt","wb");
  logline("USB Link Fixer v0.2: READ ONLY, no changes to games or links.");
  scan();
  notify("USB Link Fixer 0.2: diagnostico concluido, sem alterar arquivos.");
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER)!=0){
    logline("SDL init failed: %s",SDL_GetError());return 2;
  }
  SDL_Window* w=SDL_CreateWindow("PS4 USB Link Fixer",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,W,H,0);
  if(!w){logline("Window failed: %s",SDL_GetError());SDL_Quit();return 3;}
  SDL_Surface* surf=SDL_GetWindowSurface(w);
  SDL_Renderer* r=surf?SDL_CreateSoftwareRenderer(surf):nullptr;
  if(!r){logline("Renderer failed: %s",SDL_GetError());SDL_DestroyWindow(w);SDL_Quit();return 4;}
  bool gamepad=(SDL_NumJoysticks()>0 && SDL_IsGameController(0));
  if(gamepad)SDL_GameControllerOpen(0);
  else if(SDL_NumJoysticks()>0)SDL_JoystickOpen(0);
  bool running=true;
  while(running){
    SDL_Event e;
    while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT)running=false;
      else if(e.type==SDL_CONTROLLERBUTTONDOWN && gamepad){
        switch(e.cbutton.button){
          case SDL_CONTROLLER_BUTTON_DPAD_UP:handle(1,running);break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:handle(2,running);break;
          case SDL_CONTROLLER_BUTTON_A:handle(3,running);break;
          case SDL_CONTROLLER_BUTTON_Y:handle(4,running);break;
          case SDL_CONTROLLER_BUTTON_B:handle(5,running);break;
        }
      }else if(e.type==SDL_JOYBUTTONDOWN && !gamepad){
        if(e.jbutton.button==0)handle(3,running);
        else if(e.jbutton.button==1)handle(5,running);
        else if(e.jbutton.button==3)handle(4,running);
        else if(e.jbutton.button==11)handle(1,running);
        else if(e.jbutton.button==12)handle(2,running);
      }else if(e.type==SDL_JOYHATMOTION && !gamepad){
        if(e.jhat.value&SDL_HAT_UP)handle(1,running);
        else if(e.jhat.value&SDL_HAT_DOWN)handle(2,running);
      }else if(e.type==SDL_KEYDOWN){
        switch(e.key.keysym.sym){
          case SDLK_UP:handle(1,running);break;
          case SDLK_DOWN:handle(2,running);break;
          case SDLK_RETURN:handle(3,running);break;
          case SDLK_r:handle(4,running);break;
          case SDLK_ESCAPE:handle(5,running);break;
        }
      }
    }
    draw(r);SDL_RenderPresent(r);SDL_UpdateWindowSurface(w);SDL_Delay(20);
  }
  SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();
  if(logFile)fclose(logFile);
  return 0;
}

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

#include "font8x8_basic.h"
#include "path_match.h"
#include "fixer.h"
#include "launcher.h"
#include "catalog.h"
#include "native_catalog.h"
#include "ps4_link_io.h"

// PS4 USB Link Fixer 1.0: guarded Itemzflow symlink repair plus launch.
// Refer to Itemzflow GLES2_itemzflow.cpp for the real Itemzflow file layout.

namespace {
const int W=1280, H=720; // logical UI coordinate space
const int SCREEN_W=1920, SCREEN_H=1080; // same native window size as OpenOrbis SDL2 sample
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
  Link app, patch, dlc;
  unsigned sources=0, usb_mask=0;
};
std::vector<Game> games;
std::string message="Analisando os HDs...";
int selected=0;
bool details=false;
int confirmation=0; // 0 none, 1 repair, 2 repair+launch
bool resultVisible=false;
std::vector<std::string> repairResult;
static const char* typeLabel(int type){
  return type==0?"JOGO":type==1?"ATUALIZACAO":"DLC";
}
FILE* logFile=nullptr;
int usbCount=0;
CatalogSummary catalogInfo;
UsbfixGoldhenAccess accessInfo;
std::string goldhenStatus="TRIANGULO: preparar acesso GoldHEN";
std::string catalogStatus="Aguardando leitura";
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
  FixType type=FIX_GAME;
  if(!strcmp(ext,"app_patch.pkg"))type=FIX_UPDATE;
  if(!strcmp(ext,"app_dlc.pkg"))type=FIX_DLC;
  FixLink f=fix_inspect("",id,type);
  Link x;
  x.state=(State)f.status;
  x.source=f.internal;
  x.old_target=f.old_target;
  x.proposed=f.new_target;
  x.copies=f.matches;
  x.err=f.errno_value;
  return x;
}
static std::string detail_state(const Link& link){
  if(link.state==ACCESS_ERROR && link.err==EAGAIN)return "AGUARDANDO TRIANGULO";
  if(link.state==ACCESS_ERROR && link.err==ENOSYS)return "API DE LINK INDISPONIVEL (78)";
  return state_name(link.state);
}
static uint16_t u16(const unsigned char *p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t u32(const unsigned char *p){return uint32_t(u16(p))|(uint32_t(u16(p+2))<<16);}
static size_t safe_len(const char* s,size_t limit){size_t k=0;while(k<limit&&s[k])k++;return k;}
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
      std::string title((const char*)(buf+data),safe_len((const char*)buf+data,datalen));
      if(title.size()>60)title=title.substr(0,60);
      if(!title.empty())return title;
    }
  }
  return id;
}
static void scan(){
  games.clear();usbCount=0;selected=0;details=false;confirmation=0;resultVisible=false;repairResult.clear();
  std::vector<CatalogItem> entries;
  usbfix_collect_games("",&entries,&catalogInfo);
  int usbGames=0;
  for(int i=0;i<8;i++){
    if(catalogInfo.usb[i]>=0)usbCount++;
    if(catalogInfo.usb[i]>0)usbGames+=catalogInfo.usb[i];
  }
  for(size_t i=0;i<entries.size();i++){
    Game g;
    g.id=entries[i].id;
    g.sources=entries[i].sources;
    g.usb_mask=entries[i].usb_mask;
    g.name=read_title(g.id);
    g.app=inspect(g.id,"app.pkg","app.pkg");
    g.patch=inspect(g.id,"patch.pkg","app_patch.pkg");
    g.dlc=inspect(g.id,"ac.pkg","app_dlc.pkg");
    logline("%s | name=%s | sources=%u usb_mask=%u | app=%s [%s] -> [%s] | patch=%s [%s] -> [%s] | dlc=%s [%s] -> [%s]",
      g.id.c_str(),g.name.c_str(),g.sources,g.usb_mask,
      state_name(g.app.state),g.app.old_target.c_str(),
      g.app.proposed.c_str(),state_name(g.patch.state),g.patch.old_target.c_str(),
      g.patch.proposed.c_str(),state_name(g.dlc.state),g.dlc.old_target.c_str(),g.dlc.proposed.c_str());
    games.push_back(g);
  }
  for(size_t i=1;i<games.size();i++){
    Game tmp=games[i];size_t j=i;
    while(j>0 && tmp.name<games[j-1].name){games[j]=games[j-1];--j;}
    games[j]=tmp;
  }
  int candidates=0;
  for(const auto& g:games)if(g.app.state==BROKEN_CANDIDATE ||
     g.patch.state==BROKEN_CANDIDATE || g.dlc.state==BROKEN_CANDIDATE)candidates++;
  char line[220];
  snprintf(line,sizeof(line),"Jogos: %u | HDs Itemzflow: %d | links suspeitos: %d",
    (unsigned)games.size(),usbCount,candidates);
  message=line;
  snprintf(line,sizeof(line),"/user/app: %d (errno=%d)  appmeta: %d  ext0: %d  HD jogos: %d",
    catalogInfo.internal,catalogInfo.internal_errno,catalogInfo.appmeta,catalogInfo.ext0,usbGames);
  catalogStatus=line;
  logline("DIAGNOSTICO: %s | %s | systemmeta=%d",message.c_str(),
      catalogStatus.c_str(),catalogInfo.systemmeta);
  for(int i=0;i<8;i++)logline("USB%d itemzflow/apps count: %d",i,catalogInfo.usb[i]);
  if(games.empty() && catalogInfo.internal<0){
    message="A pasta interna nao abriu. Consulte errno abaixo.";
  }
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
  label(r,49,75,"v1.0 - REPARO COM RELATORIO DE ERROS",1,C(194,214,237));
  label(r,50,117,message,2,C(211,222,241),66);
  label(r,50,156,catalogStatus,1,C(244,196,125),130);
  label(r,50,179,goldhenStatus,1,C(134,224,244),135);
  if(games.empty()){
    label(r,95,310,"Nenhum CUSA visivel. Leia o diagnostico acima.",2,C(234,214,180),59);
  }else{
    const int shown=12;
    int start=selected-(shown/2);
    if(start<0)start=0;
    if(start>int(games.size())-shown)start=(int(games.size())-shown>0?int(games.size())-shown:0);
    for(int i=start;i<int(games.size()) && i<start+shown;i++){
      int y=200+(i-start)*36;
      const Game& g=games[i];
      if(i==selected)fill(r,35,y-7,1210,34,C(42,78,116));
      label(r,48,y,g.id,2,C(154,197,237),10);
      label(r,235,y,shorten(g.name,41),2,C(235,241,248),43);
      label(r,930,y,shorten(detail_state(g.app),19),1,status_color(g.app.state),19);
    }
  }
  if(details&&!games.empty()){
    const Game&g=games[selected];
    fill(r,70,212,1140,320,C(28,42,61));
    fill(r,70,212,1140,6,C(81,150,218));
    label(r,94,237,shorten(g.name,45),2,C(250,251,254),46);
    label(r,94,277,g.id+"   JOGO: "+detail_state(g.app),2,status_color(g.app.state),55);
    label(r,94,317,"ATUAL: "+shorten(g.app.old_target,74),1,C(211,223,239),100);
    label(r,94,339,"LOCALIZADO: "+shorten(g.app.proposed,74),1,C(211,223,239),100);
    label(r,94,377,"ATUALIZACAO: "+detail_state(g.patch),2,status_color(g.patch.state),55);
    label(r,94,417,"ATUAL: "+shorten(g.patch.old_target,74),1,C(211,223,239),100);
    label(r,94,439,"LOCALIZADO: "+shorten(g.patch.proposed,74),1,C(211,223,239),100);
    label(r,94,477,"DLC: "+detail_state(g.dlc),2,status_color(g.dlc.state),58);
    label(r,94,507,"QUADRADO: reparar   R1: executar",2,C(255,189,107),58);
    char src[108];
    snprintf(src,sizeof(src),"ORIGEM: interno=%d ext0=%d appmeta=%d USB mask=0x%02x",
      (int)((g.sources&CAT_INTERNAL)!=0),(int)((g.sources&CAT_EXT0)!=0),
      (int)((g.sources&CAT_APPMETA)!=0),g.usb_mask);
    label(r,94,531,src,1,C(210,222,242),100);
    char diags[145];
    snprintf(diags,sizeof(diags),"ERROS: jogo=%d  atualizacao=%d  DLC=%d",
             g.app.err,g.patch.err,g.dlc.err);
    label(r,94,549,diags,1,C(255,205,156),100);
  }
  if(confirmation && !games.empty()){
    fill(r,170,246,940,246,C(63,42,39));
    fill(r,170,246,940,6,C(255,185,92));
    label(r,202,272,confirmation==2?"CORRIGIR LINKS E EXECUTAR?":"CONFIRMAR REPARO DOS LINKS?",2,C(255,239,217),50);
    label(r,203,325,"Backup antes de alterar; sem mover arquivos PKG.",2,C(232,236,241),55);
    label(r,203,380,shorten(games[selected].name,42),2,C(220,222,240),42);
    label(r,203,443,"X: confirmar    O: cancelar",2,C(255,185,92),55);
  }
  if(resultVisible){
    fill(r,70,185,1140,430,C(24,43,59));
    fill(r,70,185,1140,8,C(105,202,228));
    label(r,95,208,"RELATORIO DO REPARO - VERIFICACAO REAL",2,C(235,250,255),49);
    for(size_t i=0;i<repairResult.size() && i<10;i++){
      label(r,95,259+(int)i*31,shorten(repairResult[i],125),1,
            i==1?C(255,218,161):C(226,236,245),124);
    }
    label(r,95,572,"BOLA: FECHAR RELATORIO (X NAO FECHA)",2,C(132,226,238),80);
  }
  fill(r,0,654,W,66,C(22,36,55));
  label(r,40,663,"CIMA/BAIXO: selecionar | X: detalhes/confirmar | O: voltar",1,C(207,222,237),90);
  label(r,40,683,"QUADRADO: reparar | R1: executar | TRIANGULO: acesso GoldHEN",1,C(207,222,237),90);
}
static void nav(int change){
 if(games.empty()||confirmation)return;
 selected+=change;
 if(selected<0)selected=0;
 if(selected>=(int)games.size())selected=(int)games.size()-1;
 details=false;
}
static bool launch_current(){
 if(games.empty())return false;
 std::string tid=games[selected].id,reason;
 FixLink base=fix_inspect("",tid,FIX_GAME);
 FixLink patch=fix_inspect("",tid,FIX_UPDATE);
 if((base.status!=FIX_OK && base.status!=FIX_LOCAL)||
    (patch.status!=FIX_OK && patch.status!=FIX_LOCAL && patch.status!=FIX_ABSENT)){
   message="NAO EXECUTADO: o jogo/atualizacao ainda tem problema de caminho";
   logline("LAUNCH BLOCKED %s base=%s patch=%s",tid.c_str(),
           fix_status(base.status),fix_status(patch.status));
   return false;
 }
 int err=usbfix_launch_game(tid,&reason);
 message=reason;logline("LAUNCH %s: %s",tid.c_str(),reason.c_str());
 return err>=0;
}
static void repair_selected(bool then_launch){
 if(games.empty())return;
 const std::string tid=games[selected].id;
 repairResult.clear();
 repairResult.push_back("CUSA: "+tid+" - "+games[selected].name);
 int fixed=0,failed=0,untouched=0;
 for(int i=0;i<3;i++){
   const FixType type=(FixType)i;
   const FixLink before=fix_inspect("",tid,type);
   char code[38];snprintf(code,sizeof(code)," [errno=%d]",before.errno_value);
   if(before.status==FIX_CANDIDATE){
     std::string report;
     const bool ok=fix_repair("",tid,type,&report,logFile);
     if(ok)fixed++;else failed++;
     repairResult.push_back(std::string(typeLabel(i))+": "+report);
     logline("REPAIR %s type=%d old=[%s] new=[%s] ok=%d err=%d reason=%s",
       tid.c_str(),i,before.old_target.c_str(),before.new_target.c_str(),
       (int)ok,before.errno_value,report.c_str());
   }else{
     untouched++;
     repairResult.push_back(std::string(typeLabel(i))+": NENHUMA ALTERACAO - "+
                         fix_status(before.status)+code);
     logline("REPAIR SKIP %s type=%d state=%s errno=%d",tid.c_str(),i,
          fix_status(before.status),before.errno_value);
   }
 }
 // Re-read only the selected game's links: do not lose which game was selected.
 Game& g=games[selected];
 g.app=inspect(g.id,"app.pkg","app.pkg");
 g.patch=inspect(g.id,"patch.pkg","app_patch.pkg");
 g.dlc=inspect(g.id,"ac.pkg","app_dlc.pkg");
 char summary[190];
 snprintf(summary,sizeof(summary),
          "FINAL: %d reparado(s), %d falha(s), %d sem alteracao",
          fixed,failed,untouched);
 repairResult.insert(repairResult.begin()+1,summary);
 repairResult.push_back("DEPOIS: JOGO="+std::string(fix_status((FixState)g.app.state))+
   " errno="+std::to_string(g.app.err));
 repairResult.push_back("BACKUP: /data/USBLinkFixer/ (quando criado)");
 repairResult.push_back("USB0 preservado; nenhum arquivo PKG foi movido.");
 repairResult.push_back("Se falhou, envie uma foto deste relatorio.");
 message=std::string(summary)+" | "+tid;
 logline("%s",message.c_str());
 resultVisible=true;
 // Never auto-launch after a failed or unverified attempt.
 if(then_launch && fixed>0 && failed==0 && g.app.state==VALID_LINK){
   // Require explicit R1 separately instead; no auto-run during diagnosis.
   repairResult.push_back("Abra o jogo separadamente apos conferir o resultado.");
 }
}

static bool has_candidates(){
 if(games.empty())return false;
 const std::string& id=games[selected].id;
 return fix_inspect("",id,FIX_GAME).status==FIX_CANDIDATE ||
        fix_inspect("",id,FIX_UPDATE).status==FIX_CANDIDATE ||
        fix_inspect("",id,FIX_DLC).status==FIX_CANDIDATE;
}
static void handle(int code,bool& running){
 if(resultVisible){
   if(code==5)resultVisible=false;
   return;
 }
 if(confirmation){
   if(code==3){int mode=confirmation;confirmation=0;repair_selected(mode==2);}
   else if(code==5)confirmation=0;
   return;
 }
 if(code==1)nav(-1);
 else if(code==2)nav(1);
 else if(code==3)details=!details;
 else if(code==4){
   message="Preparando acesso GoldHEN... aguarde";
   accessInfo=usbfix_request_goldhen_access();
   usbfix_io::set_authorized(accessInfo.acknowledged());
   scan();
   char report[220];
   snprintf(report,sizeof(report),"GoldHEN: SDK=%lld CF=%llu, acesso enviado=%d retorno=%lld CF=%llu",
     (long long)accessInfo.sdk,(unsigned long long)accessInfo.sdk_cf,
     (int)accessInfo.access_attempted,(long long)accessInfo.access,
     (unsigned long long)accessInfo.access_cf);
   goldhenStatus=report;
   logline("ACCESS: %s",report);
   if(accessInfo.acknowledged()){
      message=games.empty()?"Acesso solicitado; pastas ainda nao visiveis. Confira errno":
                            "Acesso GoldHEN confirmado; biblioteca atualizada";
   } else {
      message="Acesso GoldHEN nao confirmado. SDK e errno acima.";
   }
 }
 else if(code==5){if(details)details=false;else running=false;}
 else if(code==6){
   if(has_candidates()){details=true;confirmation=1;}
   else message="Nenhum link reparavel para esse jogo.";
 }
 else if(code==7){
   if(has_candidates()){details=true;confirmation=2;}
   else launch_current();
 }
}

}
int main(){
  mkdir("/data/USBLinkFixer",0700);
  logFile=fopen("/data/USBLinkFixer/scan.txt","wb");
  if(!logFile)logFile=fopen("/data/USBLinkFixer-scan.txt","wb");
  logline("USB Link Fixer v1.0: confirmation-required repair, no PKG movement.");
  usbfix_io::set_authorized(false);
  // Initialize video BEFORE accessing USB disks or reading game metadata.
  // This avoids a long filesystem scan looking like an app that never opened.
  notify("USB Link Fixer: iniciando interface...");
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)!=0){
    logline("SDL init failed: %s",SDL_GetError());
    notify("USB Link Fixer: falha ao iniciar video. Veja o log.");
    sceKernelUsleep(3500000);if(logFile)fclose(logFile);return 2;
  }
  SDL_Window* w=SDL_CreateWindow("PS4 USB Link Fixer",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,SCREEN_W,SCREEN_H,0);
  if(!w){logline("Window failed: %s",SDL_GetError());notify("USB Link Fixer: falha ao criar janela.");sceKernelUsleep(3500000);SDL_Quit();if(logFile)fclose(logFile);return 3;}
  SDL_Surface* surf=SDL_GetWindowSurface(w);
  SDL_Renderer* r=surf?SDL_CreateSoftwareRenderer(surf):nullptr;
  if(!r){logline("Renderer failed: %s",SDL_GetError());notify("USB Link Fixer: falha ao criar video.");sceKernelUsleep(3500000);SDL_DestroyWindow(w);SDL_Quit();if(logFile)fclose(logFile);return 4;}
  // Keep the proven PS4 1920x1080 output mode; scale our UI coordinates.
  // If the SDL port does not support logical scaling, keep the window open.
  int logicalResult=SDL_RenderSetLogicalSize(r,W,H);
  logline("STARTUP: 1920x1080 SDL window; logical 1280x720 scale status=%d",
          logicalResult);
  // Known working Trophy Unlocker uses 15/16; retain joystick raw fallback.
  SDL_Joystick* joystick=nullptr;
  if(SDL_NumJoysticks()>0)joystick=SDL_JoystickOpen(0);
  logline("CONTROL: SDL joysticks=%d raw_joystick_open=%d",
          SDL_NumJoysticks(),(int)(joystick!=nullptr));
  // Display a real first frame while game scanning runs synchronously.
  draw(r);
  SDL_RenderPresent(r);
  SDL_UpdateWindowSurface(w);
  logline("STARTUP: SDL window and first frame initialized");
  scan();
  logline("STARTUP: game scan completed");
  notify("USB Link Fixer: interface carregada; selecione um jogo.");
  bool running=true;
  while(running){
    SDL_Event e;
    while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT)running=false;
      else if(e.type==SDL_JOYBUTTONDOWN){
        if(e.jbutton.button==0)handle(3,running);
        else if(e.jbutton.button==1)handle(5,running);
        else if(e.jbutton.button==2)handle(6,running);
        else if(e.jbutton.button==3)handle(4,running);
        else if(e.jbutton.button==5)handle(7,running);
        else if(e.jbutton.button==11 || e.jbutton.button==15)handle(1,running);
        else if(e.jbutton.button==12 || e.jbutton.button==16)handle(2,running);
      }else if(e.type==SDL_JOYHATMOTION){
        if(e.jhat.value&SDL_HAT_UP)handle(1,running);
        else if(e.jhat.value&SDL_HAT_DOWN)handle(2,running);
      }else if(e.type==SDL_KEYDOWN){
        switch(e.key.keysym.sym){
          case SDLK_UP:handle(1,running);break;
          case SDLK_DOWN:handle(2,running);break;
          case SDLK_RETURN:handle(3,running);break;
          case SDLK_r:handle(4,running);break;
          case SDLK_F5:handle(6,running);break;
          case SDLK_F6:handle(7,running);break;
          case SDLK_ESCAPE:handle(5,running);break;
        }
      }
    }
    draw(r);SDL_RenderPresent(r);SDL_UpdateWindowSurface(w);SDL_Delay(20);
  }
  if(joystick)SDL_JoystickClose(joystick);
  SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();
  if(logFile)fclose(logFile);
  return 0;
}

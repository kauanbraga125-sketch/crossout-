#ifdef USB_FIXER_PS4
#include "fixer.h"
#include "path_match.h"
#include "ps4_link_io.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>

static const char* localdir(FixType t){
 return t==FIX_GAME?"app":t==FIX_UPDATE?"patch":"addcont";
}
static const char* localname(FixType t){
 return t==FIX_GAME?"app.pkg":t==FIX_UPDATE?"patch.pkg":"ac.pkg";
}
static const char* usbname(FixType t){
 return t==FIX_GAME?"app.pkg":t==FIX_UPDATE?"app_patch.pkg":"app_dlc.pkg";
}
bool fix_valid_id(const std::string& t){
 if(t.size()!=9)return false;
 bool hasLetter=false;
 for(int i=0;i<9;i++){
   const char c=t[i];
   if(c>='A'&&c<='Z')hasLetter=true;
   else if(c<'0'||c>'9')return false;
 }
 if(t.compare(0,4,"CUSA")==0){
   for(int i=4;i<9;i++)if(t[i]<'0'||t[i]>'9')return false;
 }
 return hasLetter;
}
const char* fix_status(FixState s){
 switch(s){
 case FIX_ABSENT:return "NAO EXISTE";
 case FIX_OK:return "LINK OK - NAO ALTERAR";
 case FIX_CANDIDATE:return "LINK USB REPARAVEL";
 case FIX_LOCAL:return "ARQUIVO ACESSIVEL - NAO ALTERAR";
 case FIX_MISSING:return "USB NAO ACESSIVEL";
 case FIX_DUPLICATE:return "COPIAS DUPLICADAS";
 case FIX_FOREIGN:return "LINK EXTERNO DESCONHECIDO";
 default:return "SEM LEITURA DO LINK";
 }
}
static std::string path_internal(const std::string& root,const std::string& tid,FixType t){
 return root+"/user/"+localdir(t)+"/"+tid+"/"+localname(t);
}
static std::string path_usb(const std::string& root,const std::string& tid,FixType t,int disk){
 char device[16];snprintf(device,sizeof(device),"usb%d",disk);
 return root+"/mnt/"+device+"/itemzflow/apps/"+tid+"/"+usbname(t);
}
static bool file_ok(const std::string& p,int* error){
 uint64_t size=0;
 int e=usbfix_io::regular_file(p,&size);
 if(error)*error=e;
 return e==0;
}
FixLink fix_inspect(const std::string& root,const std::string& tid,FixType type){
 FixLink l;
 if(!fix_valid_id(tid)||type<FIX_GAME||type>FIX_DLC){
   l.status=FIX_FOREIGN;return l;
 }
 l.internal=path_internal(root,tid,type);
 std::string target;
 const int status=usbfix_io::read_link(l.internal,&target);
 if(status){
   l.errno_value=status;
   if(status==ENOENT)l.status=FIX_ABSENT;
   else if(status==EINVAL){
     // For a normal internal game, readlink=EINVAL proves NOT a symlink.
     // Native open+fstat then confirms the original file is accessible.
     int fileError=0;
     l.status=file_ok(l.internal,&fileError)?FIX_LOCAL:FIX_DENIED;
     if(fileError)l.errno_value=fileError;
   }else l.status=FIX_DENIED;
   return l;
 }
 l.old_target=target;
 bool recognized=false;
 int lastError=0;
 for(int i=0;i<8;i++){
   const std::string p=path_usb(root,tid,type,i);
   if(usbfix_same_path(l.old_target.c_str(),p.c_str()))recognized=true;
   int err=0;
   if(file_ok(p,&err)){l.matches++;l.new_target=p;}
   else if(err!=ENOENT && err!=ENOTDIR && err!=EINVAL)lastError=err;
 }
 if(!recognized){l.status=FIX_FOREIGN;return l;}
 int currentError=0;
 if(file_ok(l.old_target,&currentError)){
   l.status=FIX_OK; // This includes live usb0: NEVER rewrite it.
   l.new_target.clear();return l;
 }
 if(l.matches==0){
   l.errno_value=lastError?lastError:currentError;
   l.status=FIX_MISSING;return l;
 }
 l.errno_value=currentError;
 l.status=l.matches==1?FIX_CANDIDATE:FIX_DUPLICATE;
 return l;
}
static void record(FILE* log,std::string* out,const std::string& s){
 if(out)*out=s;
 if(log){fprintf(log,"REPAIR: %s\n",s.c_str());fflush(log);}
}
static int swap_link(const std::string& internal,const std::string& target,
                     const std::string& temp){
 int e=usbfix_io::create_link(target,temp);
 if(e)return e;
 e=usbfix_io::rename_file(temp,internal);
 if(e)usbfix_io::remove_file(temp);
 return e;
}
bool fix_repair(const std::string& root,const std::string& tid,FixType type,
                std::string* out,FILE* log){
 const FixLink l=fix_inspect(root,tid,type);
 if(l.status!=FIX_CANDIDATE){
   char msg[240];snprintf(msg,sizeof(msg),"BLOQUEADO: %s errno=%d",
           fix_status(l.status),l.errno_value);
   record(log,out,msg);return false;
 }
 if(l.old_target.empty() || l.new_target.empty() ||
    usbfix_same_path(l.old_target.c_str(),l.new_target.c_str())){
   record(log,out,"BLOQUEADO: link sem destino verificavel");return false;
 }
 // A legacy /data/USBLinkFixer folder may be owned by the restricted
 // title identity because older builds created it BEFORE GoldHEN access.
 // Never chmod, delete, or overwrite that directory. First try a fresh,
 // separate internal directory created AFTER the access request.
 std::vector<std::string> roots;
 roots.push_back(root+"/data/ULFX00001-Backups");
 roots.push_back(root+"/data/USBLinkFixer-Recovery-v11");
 // Last fallback: a dedicated metadata-only backup directory on the USB
 // holding the valid replacement PKG. Never write backups onto USB0.
 const std::string prefix=root+"/mnt/usb";
 if(l.new_target.compare(0,prefix.size(),prefix)==0 &&
    l.new_target.size()>prefix.size()+1){
   const char digit=l.new_target[prefix.size()];
   if(digit>='1' && digit<='7' &&
      l.new_target[prefix.size()+1]=='/'){
     roots.push_back(prefix+digit+"/USBLinkFixerBackups");
   }
 }
 bool backedUp=false;
 std::string backupPath;
 std::string errors;
 int e=0;
 const std::string content=l.internal+"\n"+l.old_target+"\n"+l.new_target+"\n";
 for(const std::string& dir:roots){
   e=usbfix_io::make_dir(dir);
   if(e){
     errors+=dir+" mkdir="+std::to_string(e)+"; ";
     continue;
   }
   // Exclusive create; never reuse an existing name or replace an old backup.
   for(unsigned i=0;i<1024;i++){
     char suffix[32];
     snprintf(suffix,sizeof(suffix),"-%d-%04u.original.txt",(int)type,i);
     const std::string p=dir+"/"+tid+suffix;
     e=usbfix_io::create_exclusive_backup(p,content);
     if(e==EEXIST)continue;
     if(e){
       errors+=dir+" "+usbfix_io::backup_error_step()+"="+std::to_string(e)+"; ";
       break;
     }
     backedUp=true;
     backupPath=p;
     if(log){fprintf(log,"BACKUP VERIFIED: %s\n",p.c_str());fflush(log);}
     break;
   }
   if(backedUp)break;
 }
 if(!backedUp){
   record(log,out,"BLOQUEADO: sem backup gravavel/confirmado. "+
         (errors.empty()?std::string("locais lotados"):errors));
   return false;
 }
 // Verify USB ordering and original exact link bytes again before writing.
 const FixLink again=fix_inspect(root,tid,type);
 if(again.status!=FIX_CANDIDATE || again.matches!=1 ||
    again.old_target!=l.old_target || again.new_target!=l.new_target){
   record(log,out,"BLOQUEADO: discos ou link mudaram; backup preservado");
   return false;
 }
 const std::string temp=l.internal+".usbfix-"+std::to_string((int)type)+".tmp";
 // Only proceed if the staging pathname provably does not exist.
 std::string unused;
 const int stageErr=usbfix_io::read_link(temp,&unused);
 if(stageErr!=ENOENT){
   record(log,out,"BLOQUEADO: caminho temporario nao livre, erro="+std::to_string(stageErr));
   return false;
 }
 e=swap_link(l.internal,l.new_target,temp);
 if(e){
   record(log,out,"ERRO: criar/substituir link nao suportado errno="+std::to_string(e));
   return false;
 }
 const FixLink verified=fix_inspect(root,tid,type);
 if(verified.status!=FIX_OK ||
    !usbfix_same_path(verified.old_target.c_str(),l.new_target.c_str())){
   // Restore original only if verification failed. The original is backed up.
   const int rollback=swap_link(l.internal,l.old_target,temp);
   record(log,out,rollback?"ATENCAO: link alterado; rollback falhou errno="+std::to_string(rollback):
                      "FALHA VERIFICACAO: link original restaurado");
   return false;
 }
 record(log,out,std::string("REPARADO E VERIFICADO: ")+tid+" "+localname(type)+
         " BACKUP="+backupPath);
 return true;
}
#endif

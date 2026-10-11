#ifndef USB_FIXER_PS4
#include "fixer.h"
#include "path_match.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char* localdir(FixType t) {
 return t==FIX_GAME?"app":t==FIX_UPDATE?"patch":"addcont";
}
static const char* localname(FixType t) {
 return t==FIX_GAME?"app.pkg":t==FIX_UPDATE?"patch.pkg":"ac.pkg";
}
static const char* usbname(FixType t) {
 return t==FIX_GAME?"app.pkg":t==FIX_UPDATE?"app_patch.pkg":"app_dlc.pkg";
}
static bool file_present(const std::string& path) {
 struct stat s;return stat(path.c_str(),&s)==0 && S_ISREG(s.st_mode) && s.st_size>=0x400;
}
bool fix_valid_id(const std::string& t) {
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
const char* fix_status(FixState status) {
 switch(status){
 case FIX_ABSENT:return "NAO EXISTE";
 case FIX_OK:return "LINK OK";
 case FIX_CANDIDATE:return "REPARAVEL";
 case FIX_LOCAL:return "INTERNO - NAO ALTERAR";
 case FIX_MISSING:return "JOGO USB NAO ENCONTRADO";
 case FIX_DUPLICATE:return "VARIAS COPIAS";
 case FIX_FOREIGN:return "LINK DE OUTRO APP";
 default:return "SEM ACESSO";
 }
}
static std::string inner(const std::string& root,const std::string& tid,FixType t) {
 return root+"/user/"+localdir(t)+"/"+tid+"/"+localname(t);
}
static std::string external(const std::string& root,const std::string& tid,FixType t,int disk) {
 char n[16];snprintf(n,sizeof(n),"usb%d",disk);
 return root+"/mnt/"+n+"/itemzflow/apps/"+tid+"/"+usbname(t);
}
FixLink fix_inspect(const std::string& root,const std::string& tid,FixType type){
 FixLink l;
 if(!fix_valid_id(tid)||type<FIX_GAME||type>FIX_DLC){l.status=FIX_FOREIGN;return l;}
 l.internal=inner(root,tid,type);
 struct stat ss;
 if(lstat(l.internal.c_str(),&ss)!=0){
   l.errno_value=errno;
   l.status=(errno==ENOENT)?FIX_ABSENT:FIX_DENIED;
   return l;
 }
 if(!S_ISLNK(ss.st_mode)){l.status=FIX_LOCAL;return l;}
 char target[1024];
 ssize_t len=readlink(l.internal.c_str(),target,sizeof(target)-1);
 if(len<0 || len>=(ssize_t)sizeof(target)-1){
   l.errno_value=len<0?errno:ENAMETOOLONG;l.status=FIX_DENIED;return l;
 }
 target[len]=0;l.old_target=target;
 bool recognized=false;
 for(int i=0;i<8;i++){
   std::string p=external(root,tid,type,i);
   if(usbfix_same_path(l.old_target.c_str(),p.c_str()))recognized=true;
   if(file_present(p)){l.matches++;l.new_target=p;}
 }
 if(!recognized){l.status=FIX_FOREIGN;return l;}
 if(file_present(l.old_target)){l.status=FIX_OK;return l;}
 if(l.matches==0){l.status=FIX_MISSING;return l;}
 l.status=(l.matches==1)?FIX_CANDIDATE:FIX_DUPLICATE;
 return l;
}
static void explain(std::string* out,const std::string& result,FILE* log) {
 if(out)*out=result;
 if(log){fprintf(log,"REPAIR: %s\n",result.c_str());fflush(log);}
}
static bool atomic_swap(const std::string& link,const std::string& target,
                        const std::string& temp) {
 if(symlink(target.c_str(),temp.c_str())!=0)return false;
 if(rename(temp.c_str(),link.c_str())!=0){unlink(temp.c_str());return false;}
 return true;
}
bool fix_repair(const std::string& root,const std::string& tid,FixType type,
                std::string* out,FILE* log){
 const FixLink l=fix_inspect(root,tid,type);
 if(l.status!=FIX_CANDIDATE){
   explain(out,std::string("BLOQUEADO: ")+fix_status(l.status),log);return false;
 }
 const std::string backups=root+"/data/USBLinkFixer";
 if(mkdir(backups.c_str(),0700)!=0 && errno!=EEXIST){
   explain(out,"SEM BACKUP: sem permissao para pasta /data",log);return false;
 }
 char suffix[24];snprintf(suffix,sizeof(suffix),"%d",(int)type);
 std::string backup;
 int fd=-1;
 for(unsigned attempt=0;attempt<1024;attempt++){
   char serial[12];
   snprintf(serial,sizeof(serial),"%04u",attempt);
   backup=backups+"/"+tid+"-"+suffix+"-"+serial+".original.txt";
   fd=open(backup.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);
   if(fd>=0)break;
   if(errno!=EEXIST)break;
 }
 if(fd<0){
   explain(out,"BLOQUEADO: nao consegui criar backup exclusivo",log);
   return false;
 }
 if(log){fprintf(log,"BACKUP: %s\n",backup.c_str());fflush(log);}
 std::string record=l.internal+"\n"+l.old_target+"\n"+l.new_target+"\n";
 ssize_t wr=write(fd,record.data(),record.size());
 int e=errno;
 int synced=fsync(fd);
 int closed=close(fd);
 if(wr!=(ssize_t)record.size()||synced!=0||closed!=0){
    explain(out,"BLOQUEADO: backup incompleto",log);return false;
 }
 (void)e;
 // Strict second inspection prevents a changed USB order from being blindly applied.
 FixLink again=fix_inspect(root,tid,type);
 if(again.status!=FIX_CANDIDATE ||
    again.old_target!=l.old_target || again.new_target!=l.new_target ||
    again.matches!=1){
   explain(out,"BLOQUEADO: discos ou links mudaram durante a analise",log);return false;
 }
 // Use a unique staging path: POSIX rename replaces the link atomically.
 std::string temp=l.internal+".usbfix-"+suffix+".tmp";
 struct stat exists;
 if(lstat(temp.c_str(),&exists)==0||errno!=ENOENT){
   explain(out,"BLOQUEADO: arquivo temporario ja existe",log);return false;
 }
 if(!atomic_swap(l.internal,l.new_target,temp)){
    explain(out,std::string("ERRO: nao foi possivel escrever link (errno ")+
                std::to_string(errno)+")",log);return false;
 }
 const FixLink check=fix_inspect(root,tid,type);
 if(check.status!=FIX_OK || !usbfix_same_path(check.old_target.c_str(),l.new_target.c_str())){
    // Attempt to put the old symbolic link back immediately.
    const bool restored=atomic_swap(l.internal,l.old_target,temp);
    explain(out,restored?"FALHA: link original restaurado":
       "FALHA: consulte o backup antes de qualquer alteracao",log);
    return false;
 }
 explain(out,std::string("REPARADO: ")+tid+" / "+localname(type),log);
 return true;
}

#endif // PS4 uses native libkernel backend in fixer_ps4.cpp

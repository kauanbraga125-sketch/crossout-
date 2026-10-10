#include <orbis/libkernel.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

// v0.1: READ ONLY. Do not call unlink(), symlink(), rename(), or pkginstall().
static FILE *report;
static unsigned total=0, broken=0, ok=0, uncertain=0;
static void logf(const char *format, ...) {
    if(!report) return;
    va_list ap; va_start(ap,format); vfprintf(report,format,ap); va_end(ap);
    fputc('\n',report); fflush(report);
}
static void notify(const char *format,...) {
    OrbisNotificationRequest n; memset(&n,0,sizeof(n));
    va_list ap; va_start(ap,format); vsnprintf(n.message,sizeof(n.message),format,ap); va_end(ap);
    n.type=OrbisNotificationRequestType::NotificationRequest; n.unk3=0;
    n.useIconImageUri=1; n.targetId=-1;
    strcpy(n.iconUri,"cxml://psnotification/tex_icon_champions_league");
    sceKernelSendNotificationRequest(0,&n,3120,0);
}
static bool cusa(const char *s) {
    if(strlen(s)!=9 || strncmp(s,"CUSA",4))return false;
    for(int i=4;i<9;i++)if(!isdigit((unsigned char)s[i]))return false;
    return true;
}
static bool regular(const char *path) {
    struct stat st; return stat(path,&st)==0 && S_ISREG(st.st_mode);
}
static void check(const char *tid,const char *subdir,const char *file,const char *targetfile) {
    char internal[320],possible[8][320],linkdst[600];
    snprintf(internal,sizeof(internal),"/user/%s/%s/%s",subdir,tid,file);
    struct stat lst;
    if(lstat(internal,&lst)!=0) { logf("%s %s : no internal entry errno=%d",tid,file,errno); return; }
    total++;
    if(!S_ISLNK(lst.st_mode)) {
        logf("%s %s : NOT A LINK - untouched",tid,file); uncertain++;return;
    }
    ssize_t len=readlink(internal,linkdst,sizeof(linkdst)-1);
    if(len<0) { logf("%s %s : READLINK FAILED errno=%d",tid,file,errno); uncertain++;return; }
    linkdst[len]='\0';
    int matches=0,which=-1;
    for(int i=0;i<8;i++) {
       snprintf(possible[i],sizeof(possible[i]),"/mnt/usb%d/itemzflow/apps/%s/%s",i,tid,targetfile);
       if(regular(possible[i])) {matches++;which=i;}
    }
    struct stat deststat;
    bool current=(stat(internal,&deststat)==0 && S_ISREG(deststat.st_mode));
    if(current) {ok++;logf("%s %s : OK target=%s",tid,file,linkdst);return;}
    if(matches==1) {
       broken++;
       logf("%s %s : BROKEN old=%s suggested=%s (NO CHANGE)",tid,file,linkdst,possible[which]);
    } else {
       uncertain++;
       logf("%s %s : AMBIGUOUS old=%s match_count=%d (NO CHANGE)",tid,file,linkdst,matches);
    }
}
static void scan() {
    DIR *d=opendir("/user/app");
    if(!d) {logf("ERROR opening /user/app errno=%d",errno); notify("USB Link Fixer: /user/app inaccessible");return;}
    struct dirent *ent;
    while((ent=readdir(d))) {
        if(!cusa(ent->d_name))continue;
        char p[320];snprintf(p,sizeof(p),"/user/app/%s",ent->d_name);
        struct stat st;if(stat(p,&st)!=0 || !S_ISDIR(st.st_mode))continue;
        check(ent->d_name,"app","app.pkg","app.pkg");
        check(ent->d_name,"patch","patch.pkg","app_patch.pkg");
        // DLC IDs do not always match CUSA; deliberately left for separate scan.
    }
    closedir(d);
}
int main() {
    mkdir("/data/USBLinkFixer",0777);
    report=fopen("/data/USBLinkFixer/scan.txt","w");
    if(!report) report=fopen("/data/USBLinkFixer-scan.txt","w");
    logf("PS4 USB LINK FIXER 0.1 / DIAGNOSTIC ONLY");
    logf("No PKG modifications. If current symlink resolves, it is considered OK.");
    notify("USB Link Fixer 0.1: examining Itemzflow links (read only)");
    scan();
    logf("SUMMARY links=%u ok=%u broken_with_single_target=%u uncertain=%u",total,ok,broken,uncertain);
    if(report)fclose(report);
    notify("USB Link Fixer: %u links / %u OK / %u candidates / %u uncertain. See /data/USBLinkFixer/scan.txt",total,ok,broken,uncertain);
    for(;;)sceKernelUsleep(1000000);
    return 0;
}

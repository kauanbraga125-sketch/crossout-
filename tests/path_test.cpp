#include "../source/path_match.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

static int test(const char* label, const char* a, const char* b, bool expected) {
  bool got = usbfix_same_path(a,b);
  if(got!=expected) {
    fprintf(stderr,"FAIL %s: %s vs %s returned %d expected %d\n",label,a,b,got,expected);
    return 1;
  }
  printf("PASS %s\n",label);
  return 0;
}
int main() {
  int fail=0;
  fail+=test("one slash matches Itemzflow double slash",
    "/mnt/usb0//itemzflow/apps/CUSA12345/app.pkg",
    "/mnt/usb0/itemzflow/apps/CUSA12345/app.pkg",true);
  fail+=test("triple slash matches one slash",
    "/mnt///usb1/itemzflow///apps/CUSA12345/app_patch.pkg",
    "/mnt/usb1/itemzflow/apps/CUSA12345/app_patch.pkg",true);
  fail+=test("different drives remain different",
    "/mnt/usb0//itemzflow/apps/CUSA12345/app.pkg",
    "/mnt/usb1/itemzflow/apps/CUSA12345/app.pkg",false);
  fail+=test("different CUSA IDs remain different",
    "/mnt/usb1/itemzflow/apps/CUSA12345/app.pkg",
    "/mnt/usb1/itemzflow/apps/CUSA22222/app.pkg",false);
  fail+=test("different file variants remain different",
    "/mnt/usb1/itemzflow/apps/CUSA12345/app.pkg",
    "/mnt/usb1/itemzflow/apps/CUSA12345/app_patch.pkg",false);
  fail+=test("relative/traversal paths rejected",
    "/mnt/usb1/itemzflow/apps/CUSA12345/../app.pkg",
    "/mnt/usb1/itemzflow/apps/app.pkg",false);
  fail+=test("empty path no false match",
    "/mnt/usb0/itemzflow/apps/CUSA12345/app.pkg", "",false);
  char directory[]="/tmp/usbfix-test-XXXXXX";
  if(!mkdtemp(directory)){perror("mkdtemp");return 1;}
  char linked[512];
  snprintf(linked,sizeof(linked),"%s/link",directory);
  const char* target="/mnt/usb0//itemzflow/apps/CUSA12345/app.pkg";
  if(symlink(target,linked)){perror("symlink");return 1;}
  char data[512];
  ssize_t n=readlink(linked,data,sizeof(data)-1);
  if(n<0){perror("readlink");return 1;}
  data[n]=0;
  fail+=test("actual POSIX symlink preserves double slash",
        data,"/mnt/usb0/itemzflow/apps/CUSA12345/app.pkg",true);
  unlink(linked);
  rmdir(directory);
  if(fail)fprintf(stderr,"%d tests failed\n",fail);
  return fail?1:0;
}

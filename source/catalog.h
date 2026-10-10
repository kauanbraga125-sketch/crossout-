#pragma once
#include <string>
#include <vector>
#include <errno.h>
enum CatalogSource {
 CAT_INTERNAL=1, CAT_EXT0=2, CAT_APPMETA=4, CAT_SYSTEM_META=8, CAT_USB=16
};
struct CatalogItem {
 std::string id;
 unsigned sources;
 unsigned usb_mask;
 CatalogItem() : sources(0),usb_mask(0) {}
};
struct CatalogSummary {
 int internal,ext0,appmeta,systemmeta,usb[8];
 int internal_errno;
 CatalogSummary() : internal(0),ext0(0),appmeta(0),systemmeta(0),internal_errno(0) {
   for(int i=0;i<8;i++)usb[i]=0;
 }
};
void usbfix_collect_games(const std::string& root,
                          std::vector<CatalogItem>* result,
                          CatalogSummary* diagnostics);

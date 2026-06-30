#pragma once

#include <stddef.h>

#include <lvgl.h>

bool extractBambuSubtype(const char* material, char* out_kw, size_t out_size);
bool isSupportMaterial(const char* material_filter);
bool isSupportSpoolmanMat(const char* mat);
bool containsIgnoreCase(const char* haystack, const char* needle);
int colorDistance(const char* hex_a, const char* hex_b);
void addListMoreInfo(lv_obj_t* list, int str_id);

void fetchUnlinkedSpools();
void fetchAllSpoolsForLink(bool is_bambu, const char* material_filter, bool archived_only = false);
void patchSpoolTag(int spool_id, const char* uuid);
void showLinkEntryPopup(bool is_bambu);
void closeLinkEntryPopup();
void showIdInputPopup(bool is_bambu, bool is_copy = false);
void closeIdInputPopup();
void linkIdLookupAndPatch(int entered_id, bool is_bambu);
void showVendorList();
void showMaterialList(const char* vendor_name);
void showMaterialSubList(const char* vendor_name, const char* material_prefix);
void showFilteredSpoolList(const char* vendor_name, const char* material_prefix, const char* material_full);
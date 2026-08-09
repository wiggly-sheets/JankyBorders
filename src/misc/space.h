#pragma once
#include "helpers.h"

enum space_visibility {
  SPACE_VISIBILITY_HIDDEN,
  SPACE_VISIBILITY_NEIGHBOUR,
  SPACE_VISIBILITY_CURRENT,
};

static inline uint64_t space_id_at(CFArrayRef spaces, CFIndex index) {
  CFDictionaryRef space = CFArrayGetValueAtIndex(spaces, index);
  CFNumberRef sid_ref = CFDictionaryGetValue(space, CFSTR("id64"));
  uint64_t sid = 0;
  if (sid_ref) CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &sid);
  return sid;
}

static inline uint64_t get_active_space_id(int cid) {
  uint32_t count;
  CGGetActiveDisplayList(0, NULL, &count);

  CFStringRef uuid_ref;
  if (count == 1) {
    uint32_t did = 0;
    uint32_t count = 0;
    CGGetActiveDisplayList(1, &did, &count);
    if (count == 1) {
      CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(did);
      uuid_ref = CFUUIDCreateString(NULL, uuid);
      CFRelease(uuid);
    }
    else {
      printf("[!] ERROR (id): No active display detected!\n");
      return 0;
    }
  } else {
    uuid_ref = SLSCopyActiveMenuBarDisplayIdentifier(cid);
  }

  if (!uuid_ref) return 0;
  uint64_t sid = SLSManagedDisplayGetCurrentSpace(cid, uuid_ref);
  CFRelease(uuid_ref);
  return sid;
}

static inline bool is_space_visible(int cid, uint64_t sid) {
  CFArrayRef displays = SLSCopyManagedDisplays(cid);
  if (!displays) return false;
  uint32_t space_count = CFArrayGetCount(displays);

  for (int i = 0; i < space_count; i++) {
    if (sid == SLSManagedDisplayGetCurrentSpace(cid,
                           (CFStringRef)CFArrayGetValueAtIndex(displays, i))) {
      CFRelease(displays);
      return true;
    }
  }

  CFRelease(displays);
  return false;
}

// Returns the rendering policy for a Space on any managed display. A
// neighbouring Space is immediately before or after that display's current
// Space; Spaces farther away are hidden.
static inline enum space_visibility space_visibility_for(int cid,
                                                          uint64_t sid,
                                                          bool neighbours) {
  if (!sid) return SPACE_VISIBILITY_HIDDEN;

  CFArrayRef displays = SLSCopyManagedDisplaySpaces(cid);
  if (!displays) return SPACE_VISIBILITY_HIDDEN;

  enum space_visibility result = SPACE_VISIBILITY_HIDDEN;
  for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
    CFDictionaryRef display = CFArrayGetValueAtIndex(displays, i);
    CFStringRef uuid = CFDictionaryGetValue(display,
                                            CFSTR("Display Identifier"));
    CFArrayRef spaces = CFDictionaryGetValue(display, CFSTR("Spaces"));
    if (!uuid || !spaces) continue;

    uint64_t current_sid = SLSManagedDisplayGetCurrentSpace(cid, uuid);
    for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
      uint64_t candidate_sid = space_id_at(spaces, j);
      if (candidate_sid != sid) continue;

      if (candidate_sid == current_sid) {
        result = SPACE_VISIBILITY_CURRENT;
      } else if (neighbours
                 && ((j > 0 && space_id_at(spaces, j - 1) == current_sid)
                     || (j + 1 < CFArrayGetCount(spaces)
                         && space_id_at(spaces, j + 1) == current_sid))) {
        result = SPACE_VISIBILITY_NEIGHBOUR;
      }
      CFRelease(displays);
      return result;
    }
  }

  CFRelease(displays);
  return result;
}

#pragma once
#include <stdlib.h>
#include "border.h"
#include "hashtable.h"

extern bool g_mission_control_active;

void windows_update_inactive(struct table* windows);
void windows_update_active(struct table* windows);
void windows_update_all(struct table* windows);
void windows_update_notifications(struct table* windows);

void windows_window_update(struct table* windows, uint32_t wid);
void windows_window_refresh(struct table* windows, uint32_t wid);
void windows_window_hide(struct table* windows, uint32_t wid);
void windows_window_unhide(struct table* windows, uint32_t wid);
void windows_window_move(struct table* windows, uint32_t wid);
bool windows_window_create(struct table* windows, uint32_t wid, uint64_t sid);
bool windows_window_destroy(struct table* windows, uint32_t wid, uint32_t sid);

void windows_add_existing_windows(struct table* windows);
void windows_cleanup_orphaned_borders(struct table* windows);
void windows_hide_all_borders(struct table* windows);
void windows_mission_control_window_created(int cid, uint32_t wid);
void windows_mission_control_window_observed(int cid, uint32_t wid);
void windows_mission_control_window_destroyed(int cid, uint32_t wid);
void windows_mission_control_prune(int cid);
void windows_draw_borders_on_current_spaces(struct table* windows);
void windows_determine_and_focus_active_window(struct table* windows);
void windows_recreate_all_borders(struct table* windows);

bool mission_control_covers_display(CGRect bounds);

#ifdef DEBUG
void windows_debug_dump_mission_control_candidates(int cid);
#endif

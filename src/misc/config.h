#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static inline bool config_file_try(const char* format, const char* a,
                                   const char* b, const char* c, char* path,
                                   size_t path_size) {
  int written = snprintf(path, path_size, format, a, b, c);
  if (written <= 0 || (size_t)written >= path_size) return false;

  struct stat buffer;
  return stat(path, &buffer) == 0 && !(buffer.st_mode & S_IFDIR);
}

// Resolves the first existing config file:
//   $XDG_CONFIG_HOME/<name>/<file>, $HOME/.config/<name>/<file>, $HOME/.<file>
static inline bool config_file_path(const char* name, const char* filename,
                                   char* path, size_t path_size) {
  if (!name || !filename || !path || path_size == 0) return false;

  const char* xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] &&
      config_file_try("%s/%s/%s", xdg, name, filename, path, path_size)) {
    return true;
  }

  const char* home = getenv("HOME");
  if (!home || !home[0]) return false;

  return config_file_try("%s/.config/%s/%s", home, name, filename, path,
                         path_size) ||
         config_file_try("%s/.%s", home, filename, NULL, path, path_size);
}

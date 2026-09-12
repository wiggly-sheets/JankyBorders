#pragma once
#include "extern.h"
#include "sys/stat.h"
#include "ApplicationServices/ApplicationServices.h"
#include <stdlib.h>

static inline void debug(const char* message, ...) {
#ifdef DEBUG
  va_list va;
  va_start(va, message);
  vprintf(message, va);
  va_end(va);
#endif
}

static inline void error(const char* message, ...) {
  va_list va;
  va_start(va, message);
  vfprintf(stderr, message, va);
  va_end(va);
  exit(EXIT_FAILURE);
}

static inline bool file_exists(const char* filename) {
  struct stat buffer;
  if (stat(filename, &buffer) != 0) return false;
  if (buffer.st_mode & S_IFDIR) return false;
  return true;
}

static inline void execute_config_file(const char* name, const char* filename) {
  const char* home_directory = getenv("HOME");
  if (!home_directory || !name || !filename) return;

  size_t home_length = strlen(home_directory);
  size_t name_length = strlen(name);
  size_t filename_length = strlen(filename);
  if (home_length > SIZE_MAX - name_length - filename_length - 12) return;

  size_t size = home_length + name_length + filename_length + 12;
  char* path = malloc(size);
  if (!path) return;

  snprintf(path, size, "%s/.config/%s/%s", home_directory, name, filename);
  if (!file_exists(path)) {
    snprintf(path, size, "%s/.%s", home_directory, filename);
    if (!file_exists(path)) {
      debug("No config file found...\n");
      free(path);
      return;
    };
  }

  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  pid_t pid = fork();
  if (pid < 0) {
    printf("[!] Failed to execute config at '%s'...\n", path);
    free(path);
    return;
  }
  if (pid > 0) {
    free(path);
    return;
  }

  alarm(60);
  char* arguments[] = { "/bin/sh", path, NULL };
  execv(arguments[0], arguments);
  _exit(EXIT_FAILURE);
}

static inline CFArrayRef cfarray_of_cfnumbers(void* values,
                                             size_t size,
                                             int count,
                                             CFNumberType type) {
  if (count <= 0) {
    return CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
  }
  if (!values) return NULL;

  CFNumberRef* temp = calloc((size_t)count, sizeof(CFNumberRef));
  if (!temp) return NULL;

  for (int i = 0; i < count; ++i) {
    temp[i] = CFNumberCreate(NULL, type, ((char *)values) + (size * i));
    if (!temp[i]) {
      for (int j = 0; j < i; ++j) CFRelease(temp[j]);
      free(temp);
      return NULL;
    }
  }

  CFArrayRef result = CFArrayCreate(NULL,
                                    (const void **)temp,
                                    count,
                                    &kCFTypeArrayCallBacks);

  for (int i = 0; i < count; ++i) CFRelease(temp[i]);
  free(temp);

  return result;
}

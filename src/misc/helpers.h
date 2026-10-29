#pragma once
#include "config.h"
#include "extern.h"
#include "sys/stat.h"
#include "ApplicationServices/ApplicationServices.h"
#include <errno.h>
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
  if (!name || !filename) return;

  char path[PATH_MAX];
  if (!config_file_path(name, filename, path, sizeof(path))) {
    debug("No config file found...\n");
    return;
  }

  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  pid_t pid = fork();
  if (pid < 0) {
    printf("[!] Failed to execute config at '%s'...\n", path);
    return;
  }
  if (pid > 0) return;

  alarm(60);
  execv(path, (char*[]){ path, NULL });
  // Not a binary, or not executable: interpret it as a shell script.
  if (errno == ENOEXEC || errno == EACCES) {
    char* arguments[] = { (char*)"/bin/sh", path, NULL };
    execv(arguments[0], arguments);
  }
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

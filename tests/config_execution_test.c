#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/misc/helpers.h"

int main(void) {
  char directory[] = "/tmp/jankyborders config.XXXXXX";
  char* root = mkdtemp(directory);
  assert(root);

  char config_directory[512];
  char app_directory[512];
  char config_path[512];
  char result_path[512];
  snprintf(config_directory, sizeof(config_directory), "%s/.config", root);
  snprintf(app_directory, sizeof(app_directory), "%s/borders", config_directory);
  snprintf(config_path, sizeof(config_path), "%s/bordersrc", app_directory);
  snprintf(result_path, sizeof(result_path), "%s/result", root);
  assert(mkdir(config_directory, 0700) == 0);
  assert(mkdir(app_directory, 0700) == 0);

  int file = open(config_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  assert(file >= 0);
  const char script[] = "printf ok > \"$HOME/result\"\n";
  assert(write(file, script, sizeof(script) - 1) == sizeof(script) - 1);
  assert(close(file) == 0);
  assert(setenv("HOME", root, 1) == 0);

  execute_config_file("borders", "bordersrc");
  for (int i = 0; i < 100 && access(result_path, F_OK) != 0; ++i) {
    usleep(10000);
  }
  assert(access(result_path, F_OK) == 0);

  struct stat status;
  assert(stat(config_path, &status) == 0);
  assert((status.st_mode & S_IXUSR) == 0);

  unlink(result_path);
  unlink(config_path);
  rmdir(app_directory);
  rmdir(config_directory);
  rmdir(root);
  puts("safe config execution: ok");
  return 0;
}

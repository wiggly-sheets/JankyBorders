#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/misc/helpers.h"

static void test_xdg_precedence(void);

#define CONFIG_WAIT_ATTEMPTS 50
#define CONFIG_WAIT_INTERVAL_US 10000

static void make_temp_dir(char* path, size_t size, const char* name) {
  const char* tmp = getenv("TMPDIR");
  if (!tmp || !tmp[0]) tmp = "/tmp";
  size_t length = strlen(tmp);
  while (length > 1 && tmp[length - 1] == '/') --length;
  int written = snprintf(path, size, "%.*s/%s.XXXXXX", (int)length, tmp, name);
  assert(written > 0 && (size_t)written < size);
  assert(mkdtemp(path));
}

static void wait_for_file(const char* path) {
  // execute_config_file forks and returns no completion signal. The shell and
  // its printf finish in a few ms, so 500 ms bounds a loaded host.
  for (int i = 0;
       i < CONFIG_WAIT_ATTEMPTS && access(path, F_OK) != 0;
       ++i) {
    usleep(CONFIG_WAIT_INTERVAL_US);
  }
  assert(access(path, F_OK) == 0);
}

static void assert_file_contents(const char* path, const char* expected) {
  FILE* file = fopen(path, "r");
  assert(file);
  char buffer[8] = {0};
  fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  assert(strcmp(buffer, expected) == 0);
}

int main(void) {
  unsetenv("XDG_CONFIG_HOME");

  char directory[512];
  make_temp_dir(directory, sizeof(directory), "jankyborders-config");
  char* root = directory;

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
  wait_for_file(result_path);
  assert_file_contents(result_path, "ok");

  struct stat status;
  assert(stat(config_path, &status) == 0);
  assert((status.st_mode & S_IXUSR) == 0);

  unlink(result_path);
  unlink(config_path);
  rmdir(app_directory);
  rmdir(config_directory);
  rmdir(root);

  test_xdg_precedence();
  puts("safe config execution: ok");
  return 0;
}

static void test_xdg_precedence(void) {
  char directory[512];
  make_temp_dir(directory, sizeof(directory), "jankyborders-xdg");
  char* root = directory;

  char xdg_home[512];
  char xdg_app[512];
  char xdg_path[512];
  char home_dir[512];
  char home_config[512];
  char home_app[512];
  char home_cfg[512];
  char dot_path[512];
  char result_path[512];

  snprintf(xdg_home, sizeof(xdg_home), "%s/xdg", root);
  snprintf(xdg_app, sizeof(xdg_app), "%s/borders", xdg_home);
  snprintf(xdg_path, sizeof(xdg_path), "%s/bordersrc", xdg_app);
  snprintf(home_dir, sizeof(home_dir), "%s/home", root);
  snprintf(home_config, sizeof(home_config), "%s/.config", home_dir);
  snprintf(home_app, sizeof(home_app), "%s/borders", home_config);
  snprintf(home_cfg, sizeof(home_cfg), "%s/bordersrc", home_app);
  snprintf(dot_path, sizeof(dot_path), "%s/.bordersrc", home_dir);
  snprintf(result_path, sizeof(result_path), "%s/result", home_dir);

  assert(mkdir(xdg_home, 0700) == 0);
  assert(mkdir(xdg_app, 0700) == 0);
  assert(mkdir(home_dir, 0700) == 0);
  assert(mkdir(home_config, 0700) == 0);
  assert(mkdir(home_app, 0700) == 0);

  int xf = open(xdg_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  assert(xf >= 0);
  const char sx[] = "printf xdg > \"$HOME/result\"\n";
  assert(write(xf, sx, sizeof(sx) - 1) == sizeof(sx) - 1);
  assert(close(xf) == 0);

  int hf = open(home_cfg, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  assert(hf >= 0);
  const char sh[] = "printf home > \"$HOME/result\"\n";
  assert(write(hf, sh, sizeof(sh) - 1) == sizeof(sh) - 1);
  assert(close(hf) == 0);

  int df = open(dot_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  assert(df >= 0);
  const char sd[] = "printf dot > \"$HOME/result\"\n";
  assert(write(df, sd, sizeof(sd) - 1) == sizeof(sd) - 1);
  assert(close(df) == 0);

  assert(setenv("XDG_CONFIG_HOME", xdg_home, 1) == 0);
  assert(setenv("HOME", home_dir, 1) == 0);

  execute_config_file("borders", "bordersrc");
  wait_for_file(result_path);
  assert_file_contents(result_path, "xdg");

  // XDG unset falls back to $HOME/.config/<name>/<file>.
  unsetenv("XDG_CONFIG_HOME");
  char resolved[PATH_MAX];
  assert(config_file_path("borders", "bordersrc", resolved, sizeof(resolved)));
  assert(strcmp(resolved, home_cfg) == 0);

  // Neither XDG nor $HOME/.config falls back to $HOME/.<file>.
  unlink(home_cfg);
  rmdir(home_app);
  rmdir(home_config);
  assert(config_file_path("borders", "bordersrc", resolved, sizeof(resolved)));
  assert(strcmp(resolved, dot_path) == 0);

  // Nothing found anywhere.
  unlink(dot_path);
  assert(!config_file_path("borders", "bordersrc", resolved, sizeof(resolved)));

  unlink(result_path);
  unlink(xdg_path);
  rmdir(xdg_app);
  rmdir(xdg_home);
  rmdir(home_dir);
  rmdir(root);
}

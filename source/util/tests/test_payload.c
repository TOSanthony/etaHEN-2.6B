/* Host unit tests for libeta_payload payload helpers (no elfldr socket). */
#include "test_harness.h"
#include "test_support.h"

#include <eta/payload.h>
#include <eta/elf_name.h>
#include <elfldr_remote.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void eta_test_elfldr_reset(void);
void eta_test_elfldr_configure(bool available, pid_t launch_pid);
int eta_test_elfldr_launch_calls(void);
uint16_t eta_test_elfldr_last_port(void);
const char *eta_test_elfldr_last_path(void);
void eta_test_live_pid(pid_t pid);

static int test_is_elf(void) {
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};
  const unsigned char junk[4] = {0, 0, 0, 0};

  TEST_ASSERT_TRUE(eta_payload_is_elf(elf, sizeof(elf)));
  TEST_ASSERT_TRUE(!eta_payload_is_elf(junk, sizeof(junk)));
  TEST_ASSERT_TRUE(!eta_payload_is_elf(elf, 3));
  TEST_ASSERT_TRUE(!eta_payload_is_elf(NULL, 16));
  return 0;
}

static int test_pid_path(void) {
  char path[128];
  eta_payload_pid_path(path, sizeof(path), "mytool");
  TEST_ASSERT_STREQ("/tmp/etahen/pid/mytool.PID", path);
  return 0;
}

static int test_elf_name_stem(void) {
  TEST_ASSERT_EQ_INT(3, (int)eta_elf_name_stem_n("foo.elf", 7));
  TEST_ASSERT_EQ_INT(3, (int)eta_elf_name_stem_n("foo", 3));
  TEST_ASSERT_EQ_INT(0, (int)eta_elf_name_stem_n(".elf", 4));
  TEST_ASSERT_TRUE(eta_elf_name_has_suffix("foo.elf", 7));
  TEST_ASSERT_TRUE(!eta_elf_name_has_suffix("foo", 3));
  TEST_ASSERT_TRUE(!eta_elf_name_has_suffix(".elf.bak", 8));
  return 0;
}

static int test_elf_key_from_name(void) {
  char key[64];
  TEST_ASSERT_TRUE(eta_payload_elf_key_from_name("foo.elf", key, sizeof(key)));
  TEST_ASSERT_STREQ("foo", key);
  TEST_ASSERT_TRUE(
      eta_payload_elf_key_from_name("/data/etaHEN/payloads/bar.elf", key,
                                     sizeof(key)));
  TEST_ASSERT_STREQ("bar", key);
  TEST_ASSERT_TRUE(eta_payload_elf_key_from_name("noext", key, sizeof(key)));
  TEST_ASSERT_STREQ("noext", key);
  TEST_ASSERT_TRUE(!eta_payload_elf_key_from_name(".elf", key, sizeof(key)));
  TEST_ASSERT_TRUE(!eta_payload_elf_key_from_name(NULL, key, sizeof(key)));
  TEST_ASSERT_TRUE(!eta_payload_elf_key_from_name("", key, sizeof(key)));
  return 0;
}

static int test_path_identity(void) {
  char first[ETA_PAYLOAD_IDENTITY_SIZE];
  char repeated[ETA_PAYLOAD_IDENTITY_SIZE];
  char second[ETA_PAYLOAD_IDENTITY_SIZE];

  TEST_ASSERT_TRUE(eta_payload_identity_from_path(
      "/data/etaHEN/payloads/tool.elf", first, sizeof(first)));
  TEST_ASSERT_TRUE(eta_payload_identity_from_path(
      "/data/etaHEN/payloads/tool.elf", repeated, sizeof(repeated)));
  TEST_ASSERT_TRUE(eta_payload_identity_from_path(
      "/mnt/usb0/etaHEN/payloads/tool.elf", second, sizeof(second)));
  TEST_ASSERT_STREQ(first, repeated);
  TEST_ASSERT_TRUE(strcmp(first, second) != 0);
  TEST_ASSERT_EQ_INT(17, (int)strlen(first));
  TEST_ASSERT_TRUE(first[0] == 'p');
  TEST_ASSERT_TRUE(!eta_payload_identity_from_path("", first, sizeof(first)));
  TEST_ASSERT_TRUE(!eta_payload_identity_from_path(
      "/data/etaHEN/payloads/tool.elf", first, sizeof(first) - 1));
  return 0;
}

static int test_pid_file_roundtrip(void) {
  char path[256];
  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".PID", "", 0, path,
                                                   sizeof(path)));

  eta_payload_write_pid_file(path, 4242);
  TEST_ASSERT_EQ_INT(4242, (int)eta_payload_read_pid_file(path));

  eta_payload_write_pid_file(path, -1);
  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_read_pid_file(path));

  eta_payload_write_pid_file(path, 1);
  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_read_pid_file(path));
  eta_payload_write_pid_file(path, 0);
  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_read_pid_file(path));

  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_read_pid_file(
                             "/tmp/eta-payload-pid-missing-xyz.PID"));
  eta_test_remove_file(path);
  return 0;
}

static int test_read_file(void) {
  char path[256];
  const char payload[] = "hello-payload";
  size_t sz = 0;
  uint8_t *buf = NULL;

  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".bin", payload,
                                                   sizeof(payload) - 1, path,
                                                   sizeof(path)));
  buf = eta_payload_read_file(path, &sz);
  TEST_ASSERT_TRUE(buf != NULL);
  TEST_ASSERT_EQ_U64(sizeof(payload) - 1, sz);
  TEST_ASSERT_MEMEQ(payload, buf, sizeof(payload) - 1);
  free(buf);

  TEST_ASSERT_TRUE(eta_payload_read_file(NULL, &sz) == NULL);
  TEST_ASSERT_TRUE(eta_payload_read_file(path, NULL) == NULL);
  TEST_ASSERT_TRUE(eta_payload_read_file("/tmp/eta-missing-payload-xyz",
                                          &sz) == NULL);

  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".bin", "", 0, path,
                                                   sizeof(path)));
  TEST_ASSERT_TRUE(eta_payload_read_file(path, &sz) == NULL);
  eta_test_remove_file(path);
  return 0;
}

static int test_strict_private_loader_policy(void) {
  const char *path = "/data/etaHEN/payloads/strict-test.elf";

  eta_test_elfldr_reset();
  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_launch_elfldr(
                             "strict-test", path));
  TEST_ASSERT_EQ_INT(0, eta_test_elfldr_launch_calls());

  eta_test_elfldr_configure(true, 4242);
  TEST_ASSERT_EQ_INT(4242, (int)eta_payload_launch_elfldr(
                               "strict-test", path));
  TEST_ASSERT_EQ_INT(1, eta_test_elfldr_launch_calls());
  TEST_ASSERT_EQ_INT(ETA_ELFLDR_PORT, eta_test_elfldr_last_port());
  TEST_ASSERT_STREQ(path, eta_test_elfldr_last_path());

  eta_test_elfldr_configure(true, 0);
  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_launch_elfldr(
                             "strict-test", path));
  TEST_ASSERT_EQ_INT(2, eta_test_elfldr_launch_calls());

  eta_test_elfldr_configure(true, -1);
  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_launch_elfldr(
                             "strict-test", path));
  TEST_ASSERT_EQ_INT(3, eta_test_elfldr_launch_calls());
  return 0;
}

static int test_load_requires_real_pid(void) {
  char path[256];
  char pid_path[256];
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));

  eta_test_elfldr_reset();
  eta_test_elfldr_configure(true, 5151);
  TEST_ASSERT_TRUE(eta_payload_load(path, NULL));
  TEST_ASSERT_STREQ(path, eta_test_elfldr_last_path());

  char path_copy[256];
  snprintf(path_copy, sizeof(path_copy), "%s", path);
  const char *base = strrchr(path_copy, '/');
  base = base ? base + 1 : path_copy;
  char key[64];
  TEST_ASSERT_TRUE(eta_payload_elf_key_from_name(base, key, sizeof(key)));
  eta_payload_pid_path(pid_path, sizeof(pid_path), key);
  TEST_ASSERT_EQ_INT(5151, (int)eta_payload_read_pid_file(pid_path));

  eta_test_elfldr_configure(true, 0);
  TEST_ASSERT_TRUE(!eta_payload_load(path, NULL));
  TEST_ASSERT_EQ_INT(-1, (int)eta_payload_read_pid_file(pid_path));

  eta_test_remove_file(path);
  eta_test_remove_file(pid_path);
  return 0;
}

static int test_payload_running_without_live_pid(void) {
  TEST_ASSERT_TRUE(!eta_payload_running(NULL));
  TEST_ASSERT_TRUE(!eta_payload_running("missing-title"));
  return 0;
}

static int test_load_preserves_running_instance(void) {
  char path[256];
  char pid_path[256];
  char key[64];
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  TEST_ASSERT_TRUE(eta_payload_elf_key_from_name(base, key, sizeof(key)));
  eta_payload_pid_path(pid_path, sizeof(pid_path), key);
  eta_payload_write_pid_file(pid_path, 6262);

  eta_test_elfldr_reset();
  eta_test_live_pid(6262);
  eta_test_elfldr_configure(true, 7373);
  TEST_ASSERT_TRUE(eta_payload_load(path, NULL));
  TEST_ASSERT_EQ_INT(0, eta_test_elfldr_launch_calls());
  TEST_ASSERT_EQ_INT(6262, (int)eta_payload_read_pid_file(pid_path));

  eta_test_elfldr_reset();
  eta_test_remove_file(pid_path);
  eta_test_remove_file(path);
  return 0;
}

static int test_load_with_explicit_key(void) {
  char path[256];
  char pid_path[256];
  const char key[] = "p0123456789abcdef";
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));
  eta_test_elfldr_reset();
  eta_test_elfldr_configure(true, 8181);
  TEST_ASSERT_TRUE(eta_payload_load_with_key(path, NULL, key));
  eta_payload_pid_path(pid_path, sizeof(pid_path), key);
  TEST_ASSERT_EQ_INT(8181, (int)eta_payload_read_pid_file(pid_path));

  eta_test_remove_file(pid_path);
  eta_test_remove_file(path);
  return 0;
}

static int test_builtin_identity_payloads_allowed(void) {
  char path[256];
  const unsigned char elf[8] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};

  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(
                            ".elf", elf, sizeof(elf), path, sizeof(path)));

  eta_test_elfldr_reset();
  eta_test_elfldr_configure(true, 5151);
  TEST_ASSERT_EQ_INT(5151, (int)eta_payload_launch_elfldr(
                             "ftpsrv", path));
  TEST_ASSERT_TRUE(eta_payload_load(path, "ftpsrv.elf"));
  TEST_ASSERT_TRUE(eta_payload_load(path, "ftpsrv-ps5.elf"));
  TEST_ASSERT_TRUE(eta_payload_load(path, "kstuff.elf"));
  TEST_ASSERT_EQ_INT(4, eta_test_elfldr_launch_calls());

  char pid_path[256];
  eta_payload_pid_path(pid_path, sizeof(pid_path), "ftpsrv");
  eta_test_remove_file(pid_path);
  eta_payload_pid_path(pid_path, sizeof(pid_path), "ftpsrv-ps5");
  eta_test_remove_file(pid_path);
  eta_payload_pid_path(pid_path, sizeof(pid_path), "kstuff");
  eta_test_remove_file(pid_path);
  eta_test_remove_file(path);
  return 0;
}

static int write_marker(const char *path) {
  FILE *file = fopen(path, "wb");
  if (!file)
    return -1;
  const int written = fputs("marker", file);
  return fclose(file) == 0 && written >= 0 ? 0 : -1;
}

static int test_remove_stopped_payload(void) {
  char path[256], marker[300], config[300], neighbor[300], pid_path[256];
  char identity[ETA_PAYLOAD_IDENTITY_SIZE];
  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".elf", "\x7f" "ELF", 4,
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  snprintf(neighbor, sizeof(neighbor), "%s.neighbor.elf", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, write_marker(config));
  TEST_ASSERT_EQ_INT(0, write_marker(neighbor));
  TEST_ASSERT_TRUE(eta_payload_identity_from_path(path, identity, sizeof(identity)));
  eta_payload_pid_path(pid_path, sizeof(pid_path), identity);
  eta_payload_write_pid_file(pid_path, 4242);
  eta_test_elfldr_reset();

  TEST_ASSERT_TRUE(eta_payload_remove(path));
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && access(marker, F_OK) != 0 &&
                   access(config, F_OK) != 0 && access(pid_path, F_OK) != 0);
  TEST_ASSERT_EQ_INT(0, access(neighbor, F_OK));
  TEST_ASSERT_TRUE(eta_payload_remove(neighbor)); /* Missing sidecars are fine. */
  return 0;
}

static int test_remove_long_basename(void) {
  char directory[] = "/tmp/eta-payload-remove-XXXXXX";
  char name[256], path[512];
  TEST_ASSERT_TRUE(mkdtemp(directory) != NULL);
  memset(name, 'x', 251);
  memcpy(name + 251, ".elf", 5);
  snprintf(path, sizeof(path), "%s/%s", directory, name);
  TEST_ASSERT_EQ_INT(0, write_marker(path));
  TEST_ASSERT_TRUE(eta_payload_remove(path));
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && errno == ENOENT);
  TEST_ASSERT_EQ_INT(0, rmdir(directory));
  return 0;
}

static int test_remove_rejects_invalid_targets(void) {
  char original[256], path[300], marker[320];
  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".elf", "ELF", 3,
                                                  original, sizeof(original)));
  const char *suffixes[] = {".link.elf", ".directory.elf", ".missing.elf", ".txt"};
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
    snprintf(path, sizeof(path), "%s%s", original, suffixes[i]);
    snprintf(marker, sizeof(marker), "%s.auto_start", path);
    if (i == 0)
      TEST_ASSERT_EQ_INT(0, symlink(original, path));
    if (i == 1)
      TEST_ASSERT_EQ_INT(0, mkdir(path, 0700));
    if (i == 3)
      TEST_ASSERT_EQ_INT(0, write_marker(path));
    TEST_ASSERT_EQ_INT(0, write_marker(marker));
    TEST_ASSERT_TRUE(!eta_payload_remove(path));
    TEST_ASSERT_EQ_INT(0, access(original, F_OK));
    TEST_ASSERT_EQ_INT(0, access(marker, F_OK));
    if (i != 2)
      TEST_ASSERT_EQ_INT(0, access(path, F_OK));
    unlink(marker);
    if (i == 1)
      rmdir(path);
    else
      unlink(path);
  }
  TEST_ASSERT_TRUE(!eta_payload_remove(NULL));
  TEST_ASSERT_TRUE(!eta_payload_remove("relative.elf"));
  TEST_ASSERT_TRUE(!eta_payload_remove("/tmp/.elf"));
  snprintf(path, sizeof(path), "%s/../outside.elf", original);
  TEST_ASSERT_TRUE(!eta_payload_remove(path));
  TEST_ASSERT_EQ_INT(EINVAL, errno);
  unlink(original);
  return 0;
}

static int test_remove_preserves_running_payload(void) {
  char path[256], marker[300], config[300], pid_path[256];
  char identity[ETA_PAYLOAD_IDENTITY_SIZE];
  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".elf", "ELF", 3,
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, write_marker(config));
  TEST_ASSERT_TRUE(eta_payload_identity_from_path(path, identity, sizeof(identity)));
  eta_payload_pid_path(pid_path, sizeof(pid_path), identity);
  eta_payload_write_pid_file(pid_path, 6262);
  eta_test_live_pid(6262);

  TEST_ASSERT_TRUE(eta_payload_path_running(path));
  TEST_ASSERT_TRUE(!eta_payload_remove(path));
  TEST_ASSERT_EQ_INT(EBUSY, errno);
  TEST_ASSERT_EQ_INT(0, access(path, F_OK));
  TEST_ASSERT_EQ_INT(0, access(marker, F_OK));
  TEST_ASSERT_EQ_INT(0, access(config, F_OK));
  TEST_ASSERT_EQ_INT(6262, eta_payload_read_pid_file(pid_path));
  eta_test_elfldr_reset();
  TEST_ASSERT_TRUE(eta_payload_remove(path));
  return 0;
}

static int test_remove_after_default_launch(void) {
  char path[256], marker[300], config[300], legacy_pid[256], hashed_pid[256];
  char key[64], identity[ETA_PAYLOAD_IDENTITY_SIZE];
  const unsigned char elf[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0};
  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".elf", elf, sizeof(elf),
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, write_marker(config));
  TEST_ASSERT_TRUE(eta_payload_elf_key_from_name(path, key, sizeof(key)));
  TEST_ASSERT_TRUE(eta_payload_identity_from_path(path, identity, sizeof(identity)));
  eta_payload_pid_path(legacy_pid, sizeof(legacy_pid), key);
  eta_payload_pid_path(hashed_pid, sizeof(hashed_pid), identity);

  eta_test_elfldr_reset();
  eta_test_elfldr_configure(true, 5151);
  TEST_ASSERT_TRUE(eta_payload_load(path, NULL));
  eta_payload_write_pid_file(hashed_pid, 4242);
  eta_test_live_pid(5151);
  TEST_ASSERT_TRUE(eta_payload_path_running(path));
  TEST_ASSERT_TRUE(!eta_payload_remove(path));
  TEST_ASSERT_EQ_INT(EBUSY, errno);
  TEST_ASSERT_EQ_INT(0, access(path, F_OK));
  TEST_ASSERT_EQ_INT(0, access(marker, F_OK));
  TEST_ASSERT_EQ_INT(0, access(config, F_OK));
  TEST_ASSERT_EQ_INT(5151, eta_payload_read_pid_file(legacy_pid));
  TEST_ASSERT_EQ_INT(4242, eta_payload_read_pid_file(hashed_pid));

  eta_test_elfldr_reset();
  TEST_ASSERT_TRUE(!eta_payload_path_running(path));
  TEST_ASSERT_EQ_INT(5151, eta_payload_read_pid_file(legacy_pid));
  TEST_ASSERT_EQ_INT(4242, eta_payload_read_pid_file(hashed_pid));
  TEST_ASSERT_TRUE(eta_payload_remove(path));
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && access(marker, F_OK) != 0 &&
                   access(config, F_OK) != 0 && access(legacy_pid, F_OK) != 0 &&
                   access(hashed_pid, F_OK) != 0);
  return 0;
}

static int test_remove_reports_sidecar_failure(void) {
  char path[256], marker[300], config[300];
  TEST_ASSERT_EQ_INT(0, eta_test_write_temp_file(".elf", "ELF", 3,
                                                  path, sizeof(path)));
  snprintf(marker, sizeof(marker), "%s.auto_start", path);
  snprintf(config, sizeof(config), "%s.json", path);
  TEST_ASSERT_EQ_INT(0, write_marker(marker));
  TEST_ASSERT_EQ_INT(0, mkdir(config, 0700));
  TEST_ASSERT_TRUE(!eta_payload_remove(path));
  TEST_ASSERT_TRUE(errno != 0 && errno != ENOENT);
  TEST_ASSERT_TRUE(access(path, F_OK) != 0 && access(marker, F_OK) != 0);
  struct stat st;
  TEST_ASSERT_TRUE(lstat(config, &st) == 0 && S_ISDIR(st.st_mode));
  rmdir(config);
  return 0;
}

int test_payload_suite(void) {
  int failures = 0;
  failures += eta_test_run("payload.is_elf", test_is_elf);
  failures += eta_test_run("payload.pid_path", test_pid_path);
  failures += eta_test_run("payload.elf_name_stem", test_elf_name_stem);
  failures += eta_test_run("payload.elf_key_from_name", test_elf_key_from_name);
  failures += eta_test_run("payload.path_identity", test_path_identity);
  failures += eta_test_run("payload.pid_file_roundtrip",
                             test_pid_file_roundtrip);
  failures += eta_test_run("payload.read_file", test_read_file);
  failures += eta_test_run("payload.strict_private_loader_policy",
                             test_strict_private_loader_policy);
  failures += eta_test_run("payload.load_requires_real_pid",
                             test_load_requires_real_pid);
  failures += eta_test_run("payload.running_without_live_pid",
                             test_payload_running_without_live_pid);
  failures += eta_test_run("payload.load_preserves_running_instance",
                             test_load_preserves_running_instance);
  failures += eta_test_run("payload.load_with_explicit_key",
                             test_load_with_explicit_key);
  failures += eta_test_run("payload.builtin_identity_payloads_allowed",
                             test_builtin_identity_payloads_allowed);
  failures += eta_test_run("payload.remove_stopped", test_remove_stopped_payload);
  failures += eta_test_run("payload.remove_long_basename", test_remove_long_basename);
  failures += eta_test_run("payload.remove_invalid_targets", test_remove_rejects_invalid_targets);
  failures += eta_test_run("payload.remove_running", test_remove_preserves_running_payload);
  failures += eta_test_run("payload.remove_after_default_launch", test_remove_after_default_launch);
  failures += eta_test_run("payload.remove_sidecar_failure", test_remove_reports_sidecar_failure);
  return failures;
}

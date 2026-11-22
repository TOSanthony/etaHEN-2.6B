/* Host tests for eta_ready protocol + runtime flags. */
#include "test_harness.h"
#include <eta/ready.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int test_ready_signal_wait_clear(void) {
  const char *name = "host_test_marker";
  eta_ready_clear(name);
  TEST_ASSERT_TRUE(!eta_ready_is_set(name));
  TEST_ASSERT_TRUE(eta_ready_signal(name));
  TEST_ASSERT_TRUE(eta_ready_is_set(name));
  TEST_ASSERT_TRUE(eta_ready_wait(name, 100, 50));
  TEST_ASSERT_TRUE(eta_ready_clear(name));
  TEST_ASSERT_TRUE(!eta_ready_is_set(name));
  return 0;
}

static int test_ready_reject_slash(void) {
  TEST_ASSERT_TRUE(!eta_ready_signal("../evil"));
  TEST_ASSERT_TRUE(!eta_ready_is_set("../evil"));
  TEST_ASSERT_TRUE(!eta_ready_signal("a/b"));
  TEST_ASSERT_TRUE(!eta_ready_signal("dot.name"));
  TEST_ASSERT_TRUE(!eta_ready_signal(""));
  TEST_ASSERT_TRUE(!eta_ready_signal(NULL));
  return 0;
}

static int test_ready_timeout(void) {
  eta_ready_clear("never_set_xyz");
  TEST_ASSERT_TRUE(!eta_ready_wait("never_set_xyz", 100, 50));
  return 0;
}

static int test_ready_path_builder(void) {
  char buf[128];
  TEST_ASSERT_TRUE(eta_ready_path(ETA_FLAG_FPS_OVERLAY, buf, sizeof(buf)));
  TEST_ASSERT_TRUE(strstr(buf, "fps_overlay") != NULL);
  TEST_ASSERT_TRUE(strstr(buf, "/tmp/etahen/ready/") != NULL);
  /* buffer too small */
  TEST_ASSERT_TRUE(!eta_ready_path(ETA_FLAG_FPS_OVERLAY, buf, 8));
  TEST_ASSERT_TRUE(!eta_ready_path(ETA_FLAG_FPS_OVERLAY, NULL, 64));
  return 0;
}

static int test_runtime_flag_fps_overlay(void) {
  eta_ready_clear(ETA_FLAG_FPS_OVERLAY);
  TEST_ASSERT_TRUE(!eta_ready_is_set(ETA_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(eta_ready_signal(ETA_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(eta_ready_is_set(ETA_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(eta_ready_clear(ETA_FLAG_FPS_OVERLAY));
  TEST_ASSERT_TRUE(!eta_ready_is_set(ETA_FLAG_FPS_OVERLAY));
  return 0;
}

static int test_runtime_flag_util_booted(void) {
  eta_ready_clear(ETA_FLAG_UTIL_BOOTED);
  TEST_ASSERT_TRUE(eta_ready_signal(ETA_FLAG_UTIL_BOOTED));
  TEST_ASSERT_TRUE(eta_ready_is_set(ETA_FLAG_UTIL_BOOTED));
  eta_ready_clear(ETA_FLAG_UTIL_BOOTED);
  return 0;
}

static int test_toolbox_marker_under_runtime_root(void) {
  char path[128];
  eta_ready_clear(ETA_READY_TOOLBOX);
  TEST_ASSERT_TRUE(eta_ready_path(ETA_READY_TOOLBOX, path, sizeof(path)));
  TEST_ASSERT_TRUE(strstr(path, "/tmp/etahen/ready/toolbox") != NULL);
  TEST_ASSERT_TRUE(!eta_ready_is_set(ETA_READY_TOOLBOX));
  TEST_ASSERT_TRUE(eta_ready_signal(ETA_READY_TOOLBOX));
  TEST_ASSERT_TRUE(eta_ready_is_set(ETA_READY_TOOLBOX));
  eta_ready_clear(ETA_READY_TOOLBOX);
  TEST_ASSERT_TRUE(!eta_ready_is_set(ETA_READY_TOOLBOX));
  return 0;
}

static int test_process_instance_marker(void) {
  const char *name = "host_pid_marker";
  const pid_t expected = (pid_t)4242;
  pid_t actual = -1;

  eta_ready_clear(name);
  TEST_ASSERT_TRUE(eta_ready_signal_pid(name, expected));
  TEST_ASSERT_TRUE(eta_ready_is_set(name));
  TEST_ASSERT_TRUE(eta_ready_read_pid(name, &actual));
  TEST_ASSERT_EQ_INT((int)expected, (int)actual);
  TEST_ASSERT_TRUE(eta_ready_matches_pid(name, expected));
  TEST_ASSERT_TRUE(!eta_ready_matches_pid(name, (pid_t)4243));
  TEST_ASSERT_TRUE(eta_ready_wait_pid(name, expected, 100, 50));
  TEST_ASSERT_TRUE(!eta_ready_wait_pid(name, (pid_t)4243, 100, 50));
  eta_ready_clear(name);
  return 0;
}

static int test_process_instance_rejects_plain_marker_value(void) {
  const char *name = "host_plain_marker";
  pid_t actual = -1;

  eta_ready_clear(name);
  TEST_ASSERT_TRUE(eta_ready_signal(name));
  TEST_ASSERT_TRUE(eta_ready_is_set(name));
  TEST_ASSERT_TRUE(eta_ready_read_pid(name, &actual));
  TEST_ASSERT_EQ_INT(1, (int)actual);
  TEST_ASSERT_TRUE(!eta_ready_matches_pid(name, (pid_t)4242));
  TEST_ASSERT_TRUE(!eta_ready_signal_pid(name, (pid_t)0));
  TEST_ASSERT_TRUE(!eta_ready_read_pid(name, NULL));
  eta_ready_clear(name);
  return 0;
}

int test_ready_suite(void) {
  int failures = 0;
  failures += eta_test_run("ready_signal_wait_clear", test_ready_signal_wait_clear);
  failures += eta_test_run("ready_reject_slash", test_ready_reject_slash);
  failures += eta_test_run("ready_timeout", test_ready_timeout);
  failures += eta_test_run("ready_path_builder", test_ready_path_builder);
  failures += eta_test_run("flag_fps_overlay", test_runtime_flag_fps_overlay);
  failures += eta_test_run("flag_util_booted", test_runtime_flag_util_booted);
  failures += eta_test_run("toolbox_marker_runtime_root",
                             test_toolbox_marker_under_runtime_root);
  failures += eta_test_run("ready_process_instance",
                             test_process_instance_marker);
  failures += eta_test_run("ready_process_plain_marker_value",
                             test_process_instance_rejects_plain_marker_value);
  return failures;
}

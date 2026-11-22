#include "test_harness.h"

#include <eta/app_jailbreak_policy.hpp>

static int test_itemzflow_is_whitelisted(void) {
  const eta::AppJailbreakAllowlist allowlist{};
  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("ITEM00001", allowlist));
  TEST_ASSERT_STREQ("exact",
                    eta::app_jailbreak::whitelist_reason("ITEM00001",
                                                           allowlist));
  return 0;
}

static int test_existing_policy_is_preserved(void) {
  const eta::AppJailbreakAllowlist allowlist{};
  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("NPXS39041", allowlist));
  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("PKGI13337", allowlist));
  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("PKGI12345", allowlist));
  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("TOOL00001", allowlist));
  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("LAPY12345", allowlist));
  TEST_ASSERT_TRUE(
      !eta::app_jailbreak::is_whitelisted("CUSA12345", allowlist));
  TEST_ASSERT_STREQ("prefix", eta::app_jailbreak::whitelist_reason(
                                    "LAPY12345", allowlist));
  TEST_ASSERT_STREQ("none",
                    eta::app_jailbreak::whitelist_reason("CUSA12345",
                                                           allowlist));
  /* Prefix matching must not turn into an unanchored substring match. */
  TEST_ASSERT_TRUE(
      !eta::app_jailbreak::is_whitelisted("XLAPY1234", allowlist));
  return 0;
}

static int test_configured_policy_replaces_defaults(void) {
  eta::AppJailbreakAllowlist allowlist{};
  allowlist.exact_title_ids = {};
  allowlist.exact_title_ids[0] = "CUSA12345";
  allowlist.exact_title_id_count = 1;
  allowlist.title_id_prefixes = {};
  allowlist.title_id_prefixes[0] = "ABCD";
  allowlist.title_id_prefix_count = 1;

  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("CUSA12345", allowlist));
  TEST_ASSERT_TRUE(
      eta::app_jailbreak::is_whitelisted("ABCD00001", allowlist));
  TEST_ASSERT_TRUE(
      !eta::app_jailbreak::is_whitelisted("ITEM00001", allowlist));
  TEST_ASSERT_TRUE(
      !eta::app_jailbreak::is_whitelisted("LAPY12345", allowlist));
  return 0;
}

extern "C" int test_app_jailbreak_policy_suite(void) {
  int failures = 0;
  failures += eta_test_run("app_jailbreak.itemzflow_whitelisted",
                             test_itemzflow_is_whitelisted);
  failures += eta_test_run("app_jailbreak.existing_policy",
                             test_existing_policy_is_preserved);
  failures += eta_test_run("app_jailbreak.configured_policy",
                             test_configured_policy_replaces_defaults);
  return failures;
}

/* Host unit tests for libeta_settings schema (no PS5 SDK). */
#include "test_harness.h"

#include <eta/settings.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

static std::string temp_ini_path() {
  char tmpl[] = "/tmp/eta-settings-XXXXXX.ini";
  /* mkstemps needs suffix length 4 for ".ini" */
  int fd = mkstemps(tmpl, 4);
  if (fd < 0) {
    return {};
  }
  close(fd);
  unlink(tmpl); /* write fresh via settings_save_file */
  return std::string(tmpl);
}

static int test_defaults_and_serialize_keys(void) {
  eta::Settings s{};
  std::string text = eta::settings_serialize(s);

  TEST_ASSERT_TRUE(text.find("[meta]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("schema_version=1") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[toolbox]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("language=system") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[startup]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("open_after_load=none") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[logging]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("level=info") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("temperature_threshold_celsius=77") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("resume_reinject_delay_seconds=10") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("[rest_mode]") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("stop_utility_daemon_on_entry") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("close_running_game_on_entry") ==
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("[app_jailbreak]\n") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("[app_jailbreak]\n# enabled controls the App "
                                 "lifecycle and sandbox event listeners. "
                                 "When false,\n"
                                 "# etaHEN does not register either listener.\n"
                                 "# Available values: true, false\n"
                                 "enabled=true\n") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("edge=top") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("align=center") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("font_size=medium") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("order=fps,cpu,gpu,memory,ip,fan") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(text.find("background=true") != std::string::npos);
  TEST_ASSERT_TRUE(
      text.find("exact_title_ids=ITEM00001,NPXS39041,PKGI13337,PKGI12345,"
                "TOOL00001") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("title_id_prefixes=LAPY") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("mirror=auto") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("memory_backend") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("[kstuff]\n") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("autoload=true") != std::string::npos);
  return 0;
}

static int test_roundtrip_file(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  eta::Settings in{};
  in.fan_threshold = 55;
  in.cheats_shortcut_opt = 2;
  in.startup_open_after_load = eta::kStartupOpenHomeMenu;
  in.ui_lang = eta::kUiLanguageEn;
  in.log_level = eta::kLogLevelDebug;

  TEST_ASSERT_TRUE(eta::settings_save_file(path.c_str(), in));

  eta::Settings out{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));

  TEST_ASSERT_EQ_INT(55, out.fan_threshold);
  TEST_ASSERT_EQ_INT(2, out.cheats_shortcut_opt);
  TEST_ASSERT_EQ_INT(eta::kStartupOpenHomeMenu,
                     out.startup_open_after_load);
  TEST_ASSERT_EQ_INT(eta::kUiLanguageEn, out.ui_lang);
  TEST_ASSERT_EQ_INT(eta::kLogLevelDebug, out.log_level);
  TEST_ASSERT_EQ_INT(eta::kSettingsSchemaVersion, out.schema_version);

  unlink(path.c_str());
  return 0;
}

static int test_missing_file_defaults(void) {
  eta::Settings out{};
  out.fan_threshold = 1;
  TEST_ASSERT_TRUE(
      !eta::settings_load_file("/tmp/eta-settings-does-not-exist-xyz.ini",
                                 &out));
  /* load_file resets to defaults even on failure */
  TEST_ASSERT_EQ_INT(77, out.fan_threshold);
  return 0;
}

static int test_full_schema_roundtrip(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  eta::Settings in{};
  in.startup_open_after_load = eta::kStartupOpenHomeMenu;
  in.cheats_mirror = eta::kCheatsMirrorCnb;
  in.app_jailbreak_enabled = false;
  in.debug_app_jb_msg = true;
  in.display_tids = true;
  in.etahen_game_opts = false;
  in.rest_mode_delay_seconds = 15;
  in.enable_fan_speed = true;
  in.fan_threshold = 90;
  in.overlay_enabled = false;
  in.overlay_background = false;
  in.overlay_ram = false;
  in.overlay_cpu = false;
  in.overlay_gpu = false;
  in.overlay_fps = false;
  in.overlay_ip = true;
  in.overlay_fan = true;
  in.all_cpu_usage = true;
  in.overlay_pos = 2;
  in.overlay_align = eta::kOverlayAlignRight;
  in.overlay_font_size = eta::kOverlayFontLarge;
  in.overlay_order = {eta::kOverlayMetricFan, eta::kOverlayMetricFps,
                      eta::kOverlayMetricCpu, eta::kOverlayMetricGpu,
                      eta::kOverlayMetricMemory, eta::kOverlayMetricIp};
  in.cheats_shortcut_opt = 4;
  in.toolbox_shortcut_opt = 2;
  in.ui_lang = eta::kUiLanguageZhHans;
  in.kstuff_autoload = false;
  in.app_jailbreak_allowlist.exact_title_ids = {};
  in.app_jailbreak_allowlist.exact_title_ids[0] = "ITEM00001";
  in.app_jailbreak_allowlist.exact_title_ids[1] = "CUSA12345";
  in.app_jailbreak_allowlist.exact_title_id_count = 2;
  in.app_jailbreak_allowlist.title_id_prefixes = {};
  in.app_jailbreak_allowlist.title_id_prefixes[0] = "TEST";
  in.app_jailbreak_allowlist.title_id_prefix_count = 1;

  TEST_ASSERT_TRUE(eta::settings_save_file(path.c_str(), in));
  eta::Settings out{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));

  TEST_ASSERT_EQ_INT(in.startup_open_after_load, out.startup_open_after_load);
  TEST_ASSERT_TRUE(out.app_jailbreak_enabled == in.app_jailbreak_enabled);
  TEST_ASSERT_TRUE(out.debug_app_jb_msg == in.debug_app_jb_msg);
  TEST_ASSERT_TRUE(out.display_tids == in.display_tids);
  TEST_ASSERT_TRUE(out.etahen_game_opts == in.etahen_game_opts);
  TEST_ASSERT_EQ_U64(in.rest_mode_delay_seconds, out.rest_mode_delay_seconds);
  TEST_ASSERT_TRUE(out.enable_fan_speed == in.enable_fan_speed);
  TEST_ASSERT_EQ_INT(in.fan_threshold, out.fan_threshold);
  TEST_ASSERT_TRUE(out.overlay_enabled == in.overlay_enabled);
  TEST_ASSERT_TRUE(out.overlay_background == in.overlay_background);
  TEST_ASSERT_TRUE(out.overlay_ram == in.overlay_ram);
  TEST_ASSERT_TRUE(out.overlay_cpu == in.overlay_cpu);
  TEST_ASSERT_TRUE(out.overlay_gpu == in.overlay_gpu);
  TEST_ASSERT_TRUE(out.overlay_fps == in.overlay_fps);
  TEST_ASSERT_TRUE(out.overlay_ip == in.overlay_ip);
  TEST_ASSERT_TRUE(out.overlay_fan == in.overlay_fan);
  TEST_ASSERT_TRUE(out.all_cpu_usage == in.all_cpu_usage);
  TEST_ASSERT_EQ_INT(in.overlay_pos, out.overlay_pos);
  TEST_ASSERT_EQ_INT(in.overlay_align, out.overlay_align);
  TEST_ASSERT_EQ_INT(in.overlay_font_size, out.overlay_font_size);
  TEST_ASSERT_EQ_INT(in.overlay_order[0], out.overlay_order[0]);
  TEST_ASSERT_EQ_INT(in.overlay_order[1], out.overlay_order[1]);
  TEST_ASSERT_EQ_INT(in.overlay_order[5], out.overlay_order[5]);
  TEST_ASSERT_EQ_INT(in.cheats_shortcut_opt, out.cheats_shortcut_opt);
  TEST_ASSERT_EQ_INT(in.toolbox_shortcut_opt, out.toolbox_shortcut_opt);
  TEST_ASSERT_EQ_INT(in.ui_lang, out.ui_lang);
  TEST_ASSERT_EQ_INT(in.cheats_mirror, out.cheats_mirror);
  TEST_ASSERT_TRUE(out.kstuff_autoload == in.kstuff_autoload);
  TEST_ASSERT_EQ_U64(
      in.app_jailbreak_allowlist.exact_title_id_count,
      out.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ(
      "ITEM00001", out.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_STREQ(
      "CUSA12345", out.app_jailbreak_allowlist.exact_title_ids[1].c_str());
  TEST_ASSERT_EQ_U64(
      1, out.app_jailbreak_allowlist.title_id_prefix_count);
  TEST_ASSERT_STREQ(
      "TEST", out.app_jailbreak_allowlist.title_id_prefixes[0].c_str());

  unlink(path.c_str());
  return 0;
}

static int test_partial_ini_keeps_defaults(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());
  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[rest_mode]\n"
        "stop_utility_daemon_on_entry=true\n"
        "close_running_game_on_entry=true\n",
        f);
  fclose(f);

  eta::Settings out{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
  /* Removed keys are ignored; unspecified keys stay at defaults. */
  TEST_ASSERT_EQ_INT(eta::kStartupOpenNone, out.startup_open_after_load);
  TEST_ASSERT_EQ_INT(77, out.fan_threshold);
  TEST_ASSERT_TRUE(out.overlay_enabled);
  TEST_ASSERT_TRUE(out.overlay_background);
  TEST_ASSERT_TRUE(out.app_jailbreak_enabled);
  TEST_ASSERT_TRUE(out.kstuff_autoload);
  TEST_ASSERT_EQ_U64(10, out.rest_mode_delay_seconds);
  TEST_ASSERT_EQ_INT(eta::kCheatsMirrorAuto, out.cheats_mirror);
  TEST_ASSERT_EQ_U64(5, out.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ("ITEM00001",
                    out.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_EQ_U64(1, out.app_jailbreak_allowlist.title_id_prefix_count);
  TEST_ASSERT_STREQ("LAPY",
                    out.app_jailbreak_allowlist.title_id_prefixes[0].c_str());

  unlink(path.c_str());
  return 0;
}

static int test_startup_open_after_load_parse_policy(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[startup]\n"
        "open_after_load=home_menu\n",
        f);
  fclose(f);

  eta::Settings configured{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &configured));
  TEST_ASSERT_EQ_INT(eta::kStartupOpenHomeMenu,
                     configured.startup_open_after_load);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[startup]\n"
        "open_after_load=settings\n",
        f);
  fclose(f);

  eta::Settings invalid{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &invalid));
  TEST_ASSERT_EQ_INT(eta::kStartupOpenNone, invalid.startup_open_after_load);

  unlink(path.c_str());
  return 0;
}

static int test_rest_mode_delay_parse_policy(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[rest_mode]\n"
        "resume_reinject_delay_seconds=0\n",
        f);
  fclose(f);
  eta::Settings zero{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &zero));
  TEST_ASSERT_EQ_U64(0, zero.rest_mode_delay_seconds);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[rest_mode]\n"
        "resume_reinject_delay_seconds=255\n",
        f);
  fclose(f);
  eta::Settings max{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &max));
  TEST_ASSERT_EQ_U64(255, max.rest_mode_delay_seconds);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[rest_mode]\n"
        "resume_reinject_delay_seconds=256\n",
        f);
  fclose(f);
  eta::Settings invalid{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &invalid));
  TEST_ASSERT_EQ_U64(10, invalid.rest_mode_delay_seconds);

  unlink(path.c_str());
  return 0;
}

static int test_serialize_contains_overlay_keys(void) {
  eta::Settings s{};
  s.overlay_enabled = false;
  s.overlay_background = false;
  s.overlay_ip = true;
  s.all_cpu_usage = true;
  s.overlay_pos = 2;
  std::string text = eta::settings_serialize(s);
  TEST_ASSERT_TRUE(text.find("overlay_fps=") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("show_fps=") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("enabled=false") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("background=false") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("show_ip_address=true") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("cpu_usage_mode=per_core") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("edge=bottom") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("font_size=medium") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("order=fps,cpu,gpu,memory,ip,fan") !=
                   std::string::npos);
  return 0;
}

static int test_empty_file_loads_defaults(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());
  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fclose(f);

  eta::Settings out{};
  /* empty file: load may succeed or fall back — either way defaults applied */
  (void)eta::settings_load_file(path.c_str(), &out);
  TEST_ASSERT_EQ_INT(77, out.fan_threshold);
  unlink(path.c_str());
  return 0;
}

static int test_app_jailbreak_allowlist_parse_policy(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[app_jailbreak]\n"
        "enabled=false\n"
        "exact_title_ids=CUSA12345, CUSA12345, TEST00001\n"
        "title_id_prefixes=ABCD,TEST\n",
        f);
  fclose(f);

  eta::Settings configured{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &configured));
  TEST_ASSERT_TRUE(!configured.app_jailbreak_enabled);
  TEST_ASSERT_EQ_U64(
      2, configured.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ(
      "CUSA12345",
      configured.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_STREQ(
      "TEST00001",
      configured.app_jailbreak_allowlist.exact_title_ids[1].c_str());
  TEST_ASSERT_EQ_U64(
      2, configured.app_jailbreak_allowlist.title_id_prefix_count);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[app_jailbreak]\n"
        "exact_title_ids=none\n"
        "title_id_prefixes=none\n",
        f);
  fclose(f);

  eta::Settings disabled{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &disabled));
  TEST_ASSERT_EQ_U64(0,
                     disabled.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_EQ_U64(0,
                     disabled.app_jailbreak_allowlist.title_id_prefix_count);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[app_jailbreak]\n"
        "exact_title_ids=ITEM00001,*\n"
        "title_id_prefixes=*\n",
        f);
  fclose(f);

  eta::Settings invalid{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &invalid));
  /* Invalid replacement values retain the compiled safe defaults. */
  TEST_ASSERT_EQ_U64(5,
                     invalid.app_jailbreak_allowlist.exact_title_id_count);
  TEST_ASSERT_STREQ(
      "ITEM00001", invalid.app_jailbreak_allowlist.exact_title_ids[0].c_str());
  TEST_ASSERT_EQ_U64(1,
                     invalid.app_jailbreak_allowlist.title_id_prefix_count);
  TEST_ASSERT_STREQ(
      "LAPY", invalid.app_jailbreak_allowlist.title_id_prefixes[0].c_str());

  unlink(path.c_str());
  return 0;
}

static int test_settings_store_snapshot_update(void) {
  eta::SettingsStore store;
  eta::Settings a = store.snapshot();
  TEST_ASSERT_EQ_INT(77, a.fan_threshold);

  store.update([](eta::Settings &s) {
    s.fan_threshold = 60;
    s.enable_fan_speed = true;
  });
  eta::Settings b = store.snapshot();
  TEST_ASSERT_EQ_INT(60, b.fan_threshold);
  TEST_ASSERT_TRUE(b.enable_fan_speed);

  eta::Settings c{};
  c.overlay_ip = true;
  store.store(c);
  eta::Settings d = store.snapshot();
  TEST_ASSERT_TRUE(d.overlay_ip);
  TEST_ASSERT_TRUE(d.enable_fan_speed == false); /* full replace */
  return 0;
}

static int test_config_mtime_helpers(void) {
  /* No twin paths on host test machine → newest is 0, not newer. */
  TEST_ASSERT_TRUE(eta::settings_config_newest_mtime() == 0 ||
                   eta::settings_config_newest_mtime() >= 0);
  const time_t n = eta::settings_config_newest_mtime();
  if (n == 0) {
    TEST_ASSERT_TRUE(!eta::settings_config_is_newer_than(0));
  } else {
    TEST_ASSERT_TRUE(eta::settings_config_is_newer_than(n - 1));
    TEST_ASSERT_TRUE(!eta::settings_config_is_newer_than(n));
  }
  return 0;
}

/* An unrecognised level must fall back to the default, not to off. */
static int test_log_level_invalid_falls_back(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[logging]\nlevel=verbose\n", f);
  fclose(f);

  eta::Settings out{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_EQ_INT(eta::kLogLevelInfo, out.log_level);

  unlink(path.c_str());
  return 0;
}

static int test_language_ar_roundtrip(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  eta::Settings in{};
  in.ui_lang = eta::kUiLanguageAr;
  TEST_ASSERT_TRUE(eta::settings_serialize(in).find("language=ar") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(eta::settings_save_file(path.c_str(), in));

  eta::Settings out{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_EQ_INT(eta::kUiLanguageAr, out.ui_lang);

  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[toolbox]\nlanguage=ar-SA\n", f);
  fclose(f);
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_EQ_INT(eta::kUiLanguageAr, out.ui_lang);

  unlink(path.c_str());
  return 0;
}

static int test_language_new_locales_roundtrip(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  struct {
    int value;
    const char *canonical;
    const char *alias;
  } const cases[] = {
      {eta::kUiLanguageZhHant, "zh-Hant", "zh-TW"},
      {eta::kUiLanguageJa, "ja", "ja-JP"},
      {eta::kUiLanguageFr, "fr", "fr-FR"},
      {eta::kUiLanguageDe, "de", "deutsch"},
      {eta::kUiLanguageKo, "ko", "ko-KR"},
      {eta::kUiLanguageEs, "es", "es-MX"},
      {eta::kUiLanguagePtBr, "pt-BR", "portuguese"},
      {eta::kUiLanguageIt, "it", "italiano"},
      {eta::kUiLanguageRu, "ru", "ru-RU"},
      {eta::kUiLanguagePl, "pl", "polski"},
      {eta::kUiLanguageTh, "th", "thai"},
  };

  for (const auto &c : cases) {
    eta::Settings in{};
    in.ui_lang = c.value;
    const std::string text = eta::settings_serialize(in);
    TEST_ASSERT_TRUE(text.find(std::string("language=") + c.canonical) !=
                     std::string::npos);
    TEST_ASSERT_TRUE(eta::settings_save_file(path.c_str(), in));

    eta::Settings out{};
    TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(c.value, out.ui_lang);

    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_TRUE(f != nullptr);
    fprintf(f, "[meta]\nschema_version=1\n\n[toolbox]\nlanguage=%s\n",
            c.alias);
    fclose(f);
    TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(c.value, out.ui_lang);
  }

  unlink(path.c_str());
  return 0;
}

static int test_v0_0_10_config_keeps_kstuff_autoload(void) {
  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());
  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\n"
        "schema_version=1\n"
        "\n[toolbox]\n"
        "language=zh-Hans\n"
        "\n[kstuff]\n"
        "autoload=true\n",
        f);
  fclose(f);

  eta::Settings out{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_TRUE(out.kstuff_autoload);

  unlink(path.c_str());
  return 0;
}

static int test_clamp_fan_threshold(void) {
  TEST_ASSERT_EQ_INT(eta::kFanAutomaticThresholdCelsius,
                     eta::Settings{}.fan_threshold);
  TEST_ASSERT_EQ_INT(eta::kFanThresholdMinCelsius,
                     eta::clamp_fan_threshold(-5));
  TEST_ASSERT_EQ_INT(eta::kFanThresholdMinCelsius,
                     eta::clamp_fan_threshold(eta::kFanThresholdMinCelsius));
  TEST_ASSERT_EQ_INT(eta::kFanAutomaticThresholdCelsius,
                     eta::clamp_fan_threshold(
                         eta::kFanAutomaticThresholdCelsius));
  TEST_ASSERT_EQ_INT(eta::kFanThresholdMaxCelsius,
                     eta::clamp_fan_threshold(eta::kFanThresholdMaxCelsius));
  TEST_ASSERT_EQ_INT(eta::kFanThresholdMaxCelsius,
                     eta::clamp_fan_threshold(140));
  return 0;
}

static int test_overlay_edge_and_align(void) {
  const std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());

  struct {
    const char *edge;
    int expected_pos;
    const char *edge_canonical;
  } const edges[] = {
      {"top", 0, "top"},
      {"bottom", 2, "bottom"},
  };
  for (const auto &c : edges) {
    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_TRUE(f != nullptr);
    fprintf(f, "[meta]\nschema_version=1\n\n[overlay]\nedge=%s\n", c.edge);
    fclose(f);
    eta::Settings out{};
    TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(c.expected_pos, out.overlay_pos);
    TEST_ASSERT_EQ_INT(eta::kOverlayAlignCenter, out.overlay_align);
    TEST_ASSERT_TRUE(eta::settings_serialize(out).find(
                         std::string("edge=") + c.edge_canonical) !=
                     std::string::npos);
  }

  struct {
    const char *align;
    int expected;
    const char *canonical;
  } const aligns[] = {
      {"left", eta::kOverlayAlignLeft, "left"},
      {"center", eta::kOverlayAlignCenter, "center"},
      {"right", eta::kOverlayAlignRight, "right"},
  };
  for (const auto &c : aligns) {
    FILE *f = fopen(path.c_str(), "w");
    TEST_ASSERT_TRUE(f != nullptr);
    fprintf(f,
            "[meta]\nschema_version=1\n\n[overlay]\nedge=top\nalign=%s\n",
            c.align);
    fclose(f);
    eta::Settings out{};
    TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
    TEST_ASSERT_EQ_INT(0, out.overlay_pos);
    TEST_ASSERT_EQ_INT(c.expected, out.overlay_align);
    TEST_ASSERT_TRUE(eta::settings_serialize(out).find(
                         std::string("align=") + c.canonical) !=
                     std::string::npos);
  }

  unlink(path.c_str());
  return 0;
}

static int test_overlay_font_and_order(void) {
  TEST_ASSERT_EQ_INT(eta::kOverlayFontPtMedium,
                     eta::overlay_font_pt(eta::kOverlayFontMedium));
  TEST_ASSERT_EQ_INT(eta::kOverlayFontPtSmall,
                     eta::overlay_font_pt(eta::kOverlayFontSmall));
  TEST_ASSERT_EQ_INT(eta::kOverlayFontPtLarge,
                     eta::overlay_font_pt(eta::kOverlayFontLarge));
  TEST_ASSERT_STREQ("medium", eta::overlay_font_size_name(
                                  eta::kOverlayFontMedium));
  TEST_ASSERT_STREQ("fps", eta::overlay_metric_name(eta::kOverlayMetricFps));
  TEST_ASSERT_STREQ("memory",
                    eta::overlay_metric_name(eta::kOverlayMetricMemory));

  eta::Settings s{};
  TEST_ASSERT_EQ_INT(1, eta::overlay_metric_position(s.overlay_order,
                                                       eta::kOverlayMetricFps));
  TEST_ASSERT_EQ_INT(6, eta::overlay_metric_position(s.overlay_order,
                                                       eta::kOverlayMetricFan));
  eta::overlay_move_metric(s.overlay_order, eta::kOverlayMetricFan, 1);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricFan, s.overlay_order[0]);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricFps, s.overlay_order[1]);
  TEST_ASSERT_EQ_INT(1, eta::overlay_metric_position(s.overlay_order,
                                                       eta::kOverlayMetricFan));
  eta::overlay_move_metric(s.overlay_order, eta::kOverlayMetricFps, 6);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricFps, s.overlay_order[5]);

  std::string path = temp_ini_path();
  TEST_ASSERT_TRUE(!path.empty());
  FILE *f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[overlay]\n"
        "font_size=large\norder=fan,ip,ram\n",
        f);
  fclose(f);
  eta::Settings out{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &out));
  TEST_ASSERT_EQ_INT(eta::kOverlayFontLarge, out.overlay_font_size);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricFan, out.overlay_order[0]);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricIp, out.overlay_order[1]);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricMemory, out.overlay_order[2]);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricFps, out.overlay_order[3]);
  TEST_ASSERT_TRUE(eta::settings_serialize(out).find("font_size=large") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(eta::settings_serialize(out).find(
                       "order=fan,ip,memory,fps,cpu,gpu") != std::string::npos);

  f = fopen(path.c_str(), "w");
  TEST_ASSERT_TRUE(f != nullptr);
  fputs("[meta]\nschema_version=1\n\n[overlay]\nfont_size=tiny\norder=\n", f);
  fclose(f);
  eta::Settings invalid{};
  TEST_ASSERT_TRUE(eta::settings_load_file(path.c_str(), &invalid));
  TEST_ASSERT_EQ_INT(eta::kOverlayFontMedium, invalid.overlay_font_size);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricFps, invalid.overlay_order[0]);
  TEST_ASSERT_EQ_INT(eta::kOverlayMetricFan, invalid.overlay_order[5]);

  unlink(path.c_str());
  return 0;
}

extern "C" int test_settings_suite(void) {
  int failures = 0;
  failures += eta_test_run("settings_defaults_serialize", test_defaults_and_serialize_keys);
  failures += eta_test_run("settings_roundtrip_file", test_roundtrip_file);
  failures += eta_test_run("settings_missing_file_defaults", test_missing_file_defaults);
  failures += eta_test_run("settings_full_schema_roundtrip", test_full_schema_roundtrip);
  failures += eta_test_run("settings_partial_ini_defaults", test_partial_ini_keeps_defaults);
  failures += eta_test_run("settings_startup_open_after_load",
                             test_startup_open_after_load_parse_policy);
  failures += eta_test_run("settings_rest_mode_delay",
                             test_rest_mode_delay_parse_policy);
  failures += eta_test_run("settings_serialize_overlay_keys", test_serialize_contains_overlay_keys);
  failures += eta_test_run("settings_overlay_edge_and_align",
                             test_overlay_edge_and_align);
  failures += eta_test_run("settings_overlay_font_and_order",
                             test_overlay_font_and_order);
  failures += eta_test_run("settings_empty_file_defaults", test_empty_file_loads_defaults);
  failures += eta_test_run("settings_app_jailbreak_allowlist",
                             test_app_jailbreak_allowlist_parse_policy);
  failures += eta_test_run("settings_store_snapshot_update",
                             test_settings_store_snapshot_update);
  failures += eta_test_run("settings_config_mtime_helpers", test_config_mtime_helpers);
  failures += eta_test_run("settings_log_level_invalid",
                             test_log_level_invalid_falls_back);
  failures += eta_test_run("settings_language_ar", test_language_ar_roundtrip);
  failures += eta_test_run("settings_language_new_locales",
                             test_language_new_locales_roundtrip);
  failures += eta_test_run("settings_v0_0_10_kstuff_autoload",
                             test_v0_0_10_config_keeps_kstuff_autoload);
  failures += eta_test_run("settings_clamp_fan_threshold",
                             test_clamp_fan_threshold);
  return failures;
}

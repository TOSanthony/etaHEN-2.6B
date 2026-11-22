#include "ntp_i18n.h"

#include <stdatomic.h>
#include <string.h>

extern int sceSystemServiceParamGetInt(int param_id, int *value);

typedef struct ntp_tr_entry {
    const char *key;
    const char *en;
    const char *zh_hans;
} ntp_tr_entry;

static const ntp_tr_entry kTranslations[] = {
    {"title", "NTP Time Sync", "NTP 时间同步"},
    {"about_title", "About", "关于此插件"},
    {"about_desc",
     "Designed for PS5 consoles with depleted CMOS battery. Syncs RTC clock via NTP on boot and periodically.",
     "专为 CMOS 电池失效/没电的 PS5 设计。每次开机或断电后自动从网络高精同步系统 RTC 时钟。"},
    {"enabled_title", "Service Enabled", "启用后台服务"},
    {"enabled_desc",
     "Automatically sync time on boot and periodically in background.",
     "开启后开机自动对时并按周期定时校准时钟。"},
    {"server_title", "NTP Server", "NTP 授时服务器"},
    {"server_desc",
     "Select NTP server pool for time synchronization.",
     "选择同步时钟的目标 NTP 服务器节点。"},
    {"interval_title", "Sync Interval (Minutes)", "同步周期 (分钟)"},
    {"interval_desc",
     "Background time sync interval (1-1440 minutes).",
     "后台定时同步时间间隔 (1~1440 分钟)。"},
    {"notify_title", "Notifications", "同步完成系统通知"},
    {"notify_desc",
     "Display on-screen notification when sync completes or fails.",
     "时钟同步成功或失败时在屏幕弹出通知。"},
    {"sync_now_title", "Sync Time Now", "立即同步时间"},
    {"sync_now_desc",
     "Ensure your PS5 is connected to the network before synchronizing.",
     "同步时间需要确保您的 PS5 已连接到网络。"},
    {"srv_fr_pool", "France NTP Pool (fr.pool.ntp.org)", "法国公共授时池 (fr.pool.ntp.org)"},
    {"srv_eu_pool", "Europe NTP Pool (europe.pool.ntp.org)", "欧洲公共授时池 (europe.pool.ntp.org)"},
    {"srv_de_pool", "Germany NTP Pool (de.pool.ntp.org)", "德国公共授时池 (de.pool.ntp.org)"},
    {"srv_ptb", "PTB Germany (ptbtime1.ptb.de)", "德国联邦物理技术研究院 (ptbtime1.ptb.de)"},
    {"srv_global", "Global NTP Pool (pool.ntp.org)", "国际公共授时池 (pool.ntp.org)"},
    {"srv_cf", "Cloudflare (time.cloudflare.com)", "Cloudflare (time.cloudflare.com)"},
    {"srv_google", "Google (time.google.com)", "Google (time.google.com)"},
    {"srv_custom", "Custom Server (%s)", "自定义服务器 (%s)"},
    {"notify_syncing", "Synchronizing time with %s...", "正在从 %s 同步网络时间..."},
    {"notify_success", "Time sync successful! (RTT: %u ms)", "时间同步成功！耗时: %u ms"},
    {"notify_failed", "Time sync failed. Check network settings.", "时间同步失败，请检查网络设置。"},
};

#define TRANSLATION_COUNT (sizeof(kTranslations) / sizeof(kTranslations[0]))

static atomic_int g_current_lang = ATOMIC_VAR_INIT(NTP_LANG_EN);
static atomic_int g_initialized = ATOMIC_VAR_INIT(0);

bool ntp_i18n_refresh(void) {
    int sys_lang = 1;
    if (sceSystemServiceParamGetInt(1, &sys_lang) < 0) {
        if (!atomic_load_explicit(&g_initialized, memory_order_relaxed)) {
            atomic_store_explicit(&g_current_lang, NTP_LANG_EN, memory_order_relaxed);
            atomic_store_explicit(&g_initialized, 1, memory_order_relaxed);
            return true;
        }
        return false;
    }

    ntp_lang_t resolved = NTP_LANG_EN;
    /* 11 = Simplified Chinese, 10 = Traditional Chinese (can also use zh_hans/chinese) */
    if (sys_lang == 11 || sys_lang == 10) {
        resolved = NTP_LANG_ZH_HANS;
    } else {
        resolved = NTP_LANG_EN;
    }

    ntp_lang_t old_lang = (ntp_lang_t)atomic_load_explicit(&g_current_lang, memory_order_relaxed);
    int was_inited = atomic_load_explicit(&g_initialized, memory_order_relaxed);

    atomic_store_explicit(&g_current_lang, resolved, memory_order_relaxed);
    atomic_store_explicit(&g_initialized, 1, memory_order_relaxed);

    return (!was_inited) || (old_lang != resolved);
}

ntp_lang_t ntp_i18n_get_lang(void) {
    if (!atomic_load_explicit(&g_initialized, memory_order_relaxed)) {
        ntp_i18n_refresh();
    }
    return (ntp_lang_t)atomic_load_explicit(&g_current_lang, memory_order_relaxed);
}

const char *ntp_tr(const char *key) {
    if (!key) return "";
    ntp_lang_t lang = ntp_i18n_get_lang();
    for (size_t i = 0; i < TRANSLATION_COUNT; ++i) {
        if (strcmp(kTranslations[i].key, key) == 0) {
            return (lang == NTP_LANG_ZH_HANS) ? kTranslations[i].zh_hans : kTranslations[i].en;
        }
    }
    return key;
}

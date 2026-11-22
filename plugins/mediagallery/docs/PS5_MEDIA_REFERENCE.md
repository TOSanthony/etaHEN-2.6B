# PS5 媒体库读取与 HTTP 服务实现参考

本文档面向本仓库（etaHEN-media-gallery-plugin）的实现，整理两件事：

1. PS5 内置媒体库（截图 / 录屏）在文件系统上的确切布局；
2. 本插件如何在**不依赖 FTP**的前提下，把这些媒体以 **HTTP 接口**暴露给外部客户端。

> **职责边界（重要）**
> 本插件是 **HTTP 服务端**，不是界面。
> etaHEN 的动态 UI（ShellUI XML）**只用于插件配置页**（开关、端口等），
> **不用于罗列媒体**。媒体的浏览界面由外部客户端实现——PC 浏览器、
> 手机浏览器、或局域网内其它应用，消费本插件的 HTTP 接口即可。
>
> 因此 `ETA_UI_MAX_NODES`（256）之类的 UI 节点预算约束**不适用于媒体列表**，
> 只约束配置页。这是相对 FTP 方案最大的结构性简化。

---

## 1. PS5 侧媒体目录布局

### 1.1 根目录

```text
/user/av_contents/
```

这是索尼系统自己的媒体库根目录，即 PS5「媒体相册（Media Gallery）」所展示的内容，
不是任何第三方工具创建的目录。系统内置的截图/录屏都落在这里。

### 1.2 完整层级

```text
/user/av_contents/
├── photo/                                    # 截图
│   └── {AppId}/                              # 一级：产生捕获的系统应用 ID
│       └── {GameId}/                         # 二级：游戏 Title ID
│           └── {hash}/                       # 三级：分桶子目录（如 2f2）
│               ├── 20260914_101530_00012345.jpg     # 媒体本体
│               ├── 20260914_101530_00012345.meta    # 旁挂：JSON 元数据
│               └── 20260914_101530_00012345.jxr     # 旁挂：缩略图
└── video/                                    # 录屏
    └── {AppId}/{GameId}/{hash}/
        ├── 20260914_101530_00012345.webm            # 媒体本体
        ├── 20260914_101530_00012345.meta
        └── 20260914_101530_00012345.jxr
```

各级含义：

| 层级 | 名称 | 说明 | 可靠性 |
| --- | --- | --- | --- |
| 0 | `photo` / `video` | 媒体类型，直接决定 `type` 字段 | 固定，代码可硬编码 |
| 1 | `{AppId}` | 产生捕获的应用 ID，系统应用形如 `NPXS40087` | 固定存在 |
| 2 | `{GameId}` | 游戏 Title ID，形如 `PPSA01234` / `CUSA12345` | 固定存在，**取这一级作为 game_id** |
| 3 | `{hash}` | 分桶子目录，短十六进制串（如 `2f2`） | 命名规则未证实，**不要依赖其内容** |
| 4 | 文件名 | `YYYYMMDD_HHMMSS_xxxxxxxx.ext` | 见 1.3 |

参考实例（来自第三方 PS5 工具 airpsx 的 `MediaListHandler`）：

```text
/user/av_contents/video/NPXS40087/CUSA04555/2f2/20231030_042141_00470308.mp4
                       ^^^^^^^^^ ^^^^^^^^^ ^^^ ^^^^^^^^^^^^^^^^^^^^^^^^
                       AppId     GameId    hash 文件名
```

### 1.3 文件名规则

```text
YYYYMMDD_HHMMSS_xxxxxxxx.ext
└──┬───┘ └─┬──┘ └──┬───┘
   │       │       └─ 8 位十六进制唯一后缀
   │       └───────── 时分秒（本地时间）
   └───────────────── 年月日
```

- 全部字段定长零填充，所以**按文件名升序排序 == 按时间升序**，不需要额外排序键。
- 时间是**主机本地时间**，不带时区信息。
- 8 位后缀使同一秒内的多次捕获不会撞名。

### 1.4 旁挂文件

每个媒体本体旁边都有一组同名旁挂文件：

| 文件 | PS5（Prospero） | PS4（Orbis） | 用途 |
| --- | --- | --- | --- |
| `.{ext}` | `.jpg` / `.webm` | `.png` / `.mp4` | 媒体本体 |
| `.meta` | JSON | `.dat` | 元数据：拍摄时间、时长、标题 ID |
| `.jxr` | JPEG XR | `.png` | 系统生成的缩略图 |

**这是本方案相对 FTP 方案（Porkfolio）最大的增益**：不必再从文件名正则去猜时间，
`.meta` 是系统写出的权威元数据；`.jxr` 则让视频缩略图不再需要 ffmpeg 解码。

### 1.5 扩展名与兼容性

| 平台 | 截图 | 录屏 |
| --- | --- | --- |
| PS5 | `.jpg` | `.webm`（VP9） |
| PS4 | `.png` | `.mp4`（H.264） |

PS5 上运行 PS4 游戏时，捕获由 PS5 系统产生，因此仍是 `.webm`。
`.mp4` 只在从 PS4 主机拷贝过来的媒体里出现。建议**索引时两种都收**，
类型判断以所在目录（`photo` / `video`）为准，不要以扩展名反推类型。

### 1.6 证据与待验证项

**已核实**（多个独立来源一致）：

- 根目录 `/user/av_contents`，下分 `photo` / `video`；
- 四级层级结构（AppId / GameId / hash / 文件）；
- 文件名 `YYYYMMDD_HHMMSS_xxxxxxxx` 格式；
- PS5 用 `.meta` + `.jxr`，PS4 用 `.dat` + `.png`；
- PS5 录屏为 WebM，PS4 为 MP4。

**待真机 dump 确认**（实现时请写成容错逻辑，不要硬依赖）：

- `.meta` 的**具体字段名**。airpsx 显示其内部结构含 `segmentInfo.start`、
  `mediaTime`、`appVerTitleId` 这类键，但这是它自己构造/读取后的形态，
  未逐字确认 PS5 原始 JSON 的键名。→ 见 §3.2 的候选键表。
- `{hash}` 子目录的命名规则与是否恒存在。
- `{AppId}` 一级是否在所有固件/所有捕获类型下都存在。
- 本插件进程能否读到 `/user`（取决于权限，见 §2.1）。

---

## 2. 读取策略：本地直读

### 2.1 权限：先提权

`/user` 是系统的用户分区。插件必须先把自身凭据提升到系统权限，
否则 `opendir("/user/av_contents")` 会直接 `EACCES`。

现成范式见同级仓库 `etaHEN-ftpsrv-plugin/source/main.c`：

```c
#include <ps5/kernel.h>
#include <sys/syscall.h>

#define PLUGIN_AUTH_ID UINT64_C(0x4801000000000013)

if (kernel_set_ucred_authid(getpid(), PLUGIN_AUTH_ID) != 0) {
    return ETA_E_PERMISSION;
}
```

需要链接 `kernel_sys`，并在描述符里声明 `ETA_PLUGIN_CAP_KERNEL`。

**提权失败必须优雅降级**：不要直接退出，而是把服务标记为「不可用」，
在配置页与 `/api/v1/health` 里明确回报原因，日志写清 errno。

### 2.2 枚举

用标准 libc 递归遍历即可，不需要任何额外依赖：

```c
#define MEDIA_ROOT "/user/av_contents"
#define MEDIA_MAX_DEPTH 6

typedef struct media_entry {
    char     path[512];      /* 完整绝对路径，用于后续 open() */
    char     app_id[16];     /* 一级目录 */
    char     game_id[16];    /* 二级目录，对外暴露的 game_id */
    char     name[128];      /* 文件名（含扩展名） */
    uint64_t size;
    int64_t  captured_at;    /* Unix 秒，0 表示未知 */
    uint32_t duration_ms;    /* 仅视频，0 表示未知 */
    uint8_t  kind;           /* 0 = photo, 1 = video */
} media_entry;
```

递归时**把 app_id / game_id 沿调用栈带下去**，而不是扫到文件后再回头切路径字符串——
层级位置在递归起点就已确定，这样能彻底避开 Porkfolio 那类「在路径段里找第一个像 Title ID 的段」
的误判（`NPXS40087` 同样匹配 `[A-Z]{2,4}\d{5}`，会被错当成游戏 ID）。

```c
static int is_media_file(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return strcasecmp(dot, ".jpg")  == 0 || strcasecmp(dot, ".jpeg") == 0 ||
           strcasecmp(dot, ".png")  == 0 || strcasecmp(dot, ".webm") == 0 ||
           strcasecmp(dot, ".mp4")  == 0;
}

static void scan_dir(media_index *index, const char *dir, int depth,
                     uint8_t kind, const char *app_id, const char *game_id) {
    if (depth > MEDIA_MAX_DEPTH) return;
    DIR *handle = opendir(dir);
    if (!handle) { index->skipped_dirs++; return; }   /* 记录并继续，不要中断整轮扫描 */

    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char child[512];
        if (snprintf(child, sizeof(child), "%s/%s", dir, entry->d_name) >=
            (int)sizeof(child))
            continue;                                  /* 超长路径直接丢弃 */

        struct stat info;
        if (stat(child, &info) != 0) continue;

        if (S_ISDIR(info.st_mode)) {
            /* 深度 0 = photo|video，深度 1 = AppId，深度 2 = GameId */
            const char *next_app  = (depth == 0) ? entry->d_name : app_id;
            const char *next_game = (depth == 1) ? entry->d_name : game_id;
            scan_dir(index, child, depth + 1, kind, next_app, next_game);
            continue;
        }

        if (!is_media_file(entry->d_name)) continue;   /* .meta / .jxr 在此被自然跳过 */
        media_index_push(index, child, entry->d_name, &info, kind, app_id, game_id);
    }
    closedir(handle);
}
```

要点：

- **深度上限**必须有。异常目录树（或未来系统改版）不能让插件卡死。
- **`.meta` / `.jxr` 靠扩展名过滤即可**，不需要额外判断。
- **单目录失败不致命**：`opendir` 失败只计数并继续，最终在 health 里回报跳过的目录数。
- 图片和视频分别以 `/user/av_contents/photo`、`/user/av_contents/video` 为根各跑一次。

### 2.3 元数据三级回退

每个媒体条目的拍摄时间按以下优先级取值：

| 优先级 | 来源 | 说明 |
| --- | --- | --- |
| 1 | `.meta` JSON | 权威值，见 §3 |
| 2 | 文件名 | `YYYYMMDD_HHMMSS` → `mktime()` |
| 3 | `stat.st_mtime` | 最后手段 |

`duration_ms` 只有 `.meta` 能提供（视频）；拿不到就填 0，由客户端决定如何显示。

### 2.4 索引与缓存

- **索引常驻内存**，不要每次请求都重新遍历目录（媒体多时单次遍历可到秒级）。
- 索引结构建议按 `game_id` 分桶，便于 `/api/v1/games` 直接聚合。
- **刷新时机**：显式 `POST /api/v1/rescan`；或启动时一次 + 可选的文件系统变化检测。
- **规模上限**：PS5 媒体可能上万条。给索引设一个硬上限（例如 50000 条），
  超出时停止收录并置 `truncated` 标记，避免把内存吃满。
- 每条记录只存元数据（约 700 字节），**不要缓存文件内容**。

### 2.5 只读原则

- 对 `/user` **只读**。不写入、不重命名、不删除。
- 需要「删除媒体」这类破坏性能力时，**不要**直接操作 `/user`，
  改由系统「媒体相册」完成；插件最多只做导出（见 §4.6）。
- 不持有长期打开的文件描述符：每次请求按需 `open`，用完立即 `close`。

---

## 3. `.meta` 与 `.jxr` 的使用

### 3.1 为什么值得用

FTP 方案（Porkfolio）必须绕开两件事：FTP 的 `modifiedAt` 常缺失或有偏移，
所以只能从文件名正则取时间；而视频缩略图要下载前 4–10 MB 再用 ffmpeg 抽帧。
本地直读 + 旁挂文件把这两个问题都消掉了：

| 需求 | FTP 方案 | 本方案 |
| --- | --- | --- |
| 拍摄时间 | 解析文件名（脆弱） | 读 `.meta`（权威） |
| 视频时长 | 无法获得 | 读 `.meta` |
| 视频缩略图 | 下 4–10 MB + ffmpeg 抽帧 | 直接读 `.jxr` 旁挂文件 |
| 游戏名 | 另建索引关联 | 层级直接给出 Title ID |

### 3.2 极简 `.meta` 解析

`.meta` 是小体积 JSON。为了不引入第三方 JSON 库，写一个**容错取值器**即可：
定位 `"key"` → 跳过 `:` 与空白 → 读取数字或字符串。

```c
/* 在 json 中查找 "key"，取出其字符串值。找到返回 1。 */
static int meta_get_string(const char *json, const char *key,
                           char *out, size_t capacity) {
    char pattern[64];
    if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) >= (int)sizeof(pattern))
        return 0;

    const char *cursor = strstr(json, pattern);
    if (!cursor) return 0;
    cursor += strlen(pattern);

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')
        cursor++;
    if (*cursor != ':') return 0;
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')
        cursor++;
    if (*cursor != '"') return 0;              /* 不是字符串 */
    cursor++;

    size_t written = 0;
    while (*cursor && *cursor != '"' && written + 1 < capacity) {
        if (*cursor == '\\' && cursor[1]) cursor++;   /* 粗略处理转义 */
        out[written++] = *cursor++;
    }
    out[written] = '\0';
    return written > 0;
}

/* 取整数值（数字或带引号的数字都接受） */
static int meta_get_int(const char *json, const char *key, long long *out);
```

读取时**整体读入一个小缓冲**（`.meta` 通常几 KB，上限给 16 KB），不要流式解析。

**候选键表**（按顺序尝试，命中即用，全部失败则回退到文件名）：

| 目标 | 候选键 |
| --- | --- |
| 拍摄时间（Unix 秒） | `segmentInfo.start`、`start`、`ctime`、`createdAt` |
| 拍摄时间（毫秒） | 同上，值 > 1e11 时按毫秒处理 |
| 时长（毫秒） | `mediaTime`、`duration`、`durationMs` |
| Title ID | `appVerTitleId`、`titleId`、`appTitleId` |

⚠️ 这些键名**尚未逐字验证**（见 §1.6）。所以：

1. 首次在真机上跑通时，请 dump 一个 `.meta` 的原始内容，把实际键名回填到本文档；
2. 取值器必须**全部候选都失败也能正常工作**，退回文件名时间；
3. 建议加一个调试端点 `GET /api/v1/debug/meta?path=...` 原样返回 `.meta` 内容，
   方便一次性确认字段。

### 3.3 `.jxr` 缩略图怎么用

`.jxr` 是 **JPEG XR** 格式，浏览器（Chrome / Firefox / Safari）**都不能直接渲染**。
所以不能把它当成 `<img src>` 的直出源。三个可选处理：

| 方案 | 做法 | 取舍 |
| --- | --- | --- |
| A. 原样透出 | `/api/v1/thumb` 返回 `.jxr` 字节，`Content-Type: image/jxr` | 零成本；客户端自己找解码器（Windows 照片、部分库支持） |
| B. 忽略 | 视频缩略图接口直接 404，客户端显示占位图 | 最简单；体验降级 |
| C. 客户端转码 | 客户端拉 `.jxr` 后本地转 PNG | 把复杂度推给客户端，但服务端保持零依赖 |

**推荐 A + B 组合**：接口存在、返回原始 `.jxr`，客户端能做就做、不能做就显示占位。
不建议在插件里做转码（PS5 上无现成解码器，引入依赖不划算）。

**截图不需要缩略图**：`.jpg` / `.png` 本体浏览器直接可渲染，
`/api/v1/thumb` 对截图直接返回本体即可（媒体相册的截图通常 1–2 MB，可接受）。
如果在意流量，可让客户端用 `?max=640` 之类参数自行缩放——服务端不做缩放。

---

## 4. HTTP 接口设计

### 4.1 通用约定

- 协议：HTTP/1.1，明文（局域网内自用；etaHEN 本身也是明文 FTP，安全模型一致）。
- 绑定：默认 `0.0.0.0`，端口可配置（默认建议 `8380`，避开 ftpsrv 的 `1337`）。
- 编码：JSON 响应统一 `application/json; charset=utf-8`。
- 版本前缀：`/api/v1/`。
- 错误结构：

```json
{ "error": { "code": "not_found", "message": "media path is not indexed" } }
```

| HTTP | code | 含义 |
| --- | --- | --- |
| 400 | `bad_request` | 参数缺失或非法 |
| 403 | `forbidden` | 路径不在允许的根目录下 |
| 404 | `not_found` | 条目不存在 |
| 405 | `method_not_allowed` | 方法不支持 |
| 500 | `internal` | 服务端错误 |
| 503 | `not_ready` | 提权失败或索引未就绪 |

### 4.2 端点总览

| 方法 | 路径 | 作用 |
| --- | --- | --- |
| `GET` | `/api/v1/health` | 服务与权限状态，客户端做可用性探测 |
| `GET` | `/api/v1/games` | 按游戏聚合的统计 |
| `GET` | `/api/v1/media` | 媒体列表，支持筛选/分页/排序 |
| `GET` | `/api/v1/media/{id}` | 单条详情 |
| `GET` | `/api/v1/thumb/{id}` | 缩略图（截图=本体，视频=`.jxr`） |
| `GET` | `/api/v1/file/{id}` | 媒体本体，**支持 Range** |
| `POST` | `/api/v1/rescan` | 触发重新索引 |
| `GET` | `/api/v1/debug/meta/{id}` | 调试：原样返回 `.meta`（默认关闭） |

**用 `{id}` 而不是路径参数**：ID 是插件索引里的稳定序号或短哈希，
避免把文件路径暴露在 URL 里，也让路径校验集中在服务端一处完成。

### 4.3 `GET /api/v1/health`

```json
{
  "status": "ok",
  "plugin": "MEDI00001",
  "version": "1.00",
  "privileged": true,
  "indexed": 1284,
  "truncated": false,
  "skipped_dirs": 0,
  "indexed_at": 1789012345,
  "media_root": "/user/av_contents"
}
```

`privileged: false` 或 `status: "degraded"` 时，客户端应显示明确提示而不是空列表。

### 4.4 `GET /api/v1/media`

查询参数：

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `type` | `all` | `all` / `photo` / `video` |
| `game` | — | 精确匹配 `game_id`，可重复 |
| `since` / `until` | — | Unix 秒，按 `captured_at` 过滤 |
| `sort` | `date_desc` | `date_desc` / `date_asc` / `size_desc` / `name_asc` |
| `limit` | `100` | 上限硬性 ≤ 500 |
| `offset` | `0` | 分页偏移 |

响应：

```json
{
  "total": 1284,
  "offset": 0,
  "limit": 100,
  "items": [
    {
      "id": "p000042",
      "type": "photo",
      "game_id": "PPSA01234",
      "app_id": "NPXS40087",
      "name": "20260914_101530_00012345.jpg",
      "size": 1843200,
      "captured_at": 1789012345,
      "duration_ms": 0,
      "has_thumb": true,
      "thumb_url": "/api/v1/thumb/p000042",
      "file_url": "/api/v1/file/p000042"
    }
  ]
}
```

### 4.5 `GET /api/v1/thumb/{id}` 与 `GET /api/v1/file/{id}`

`file` **必须实现 Range**，否则浏览器 `<video>` 无法拖动进度条、甚至无法播放。

请求：

```http
GET /api/v1/file/v000017 HTTP/1.1
Range: bytes=0-1048575
```

响应：

```http
HTTP/1.1 206 Partial Content
Content-Type: video/webm
Content-Length: 1048576
Content-Range: bytes 0-1048575/31457280
Accept-Ranges: bytes
```

实现要点：

- 解析 `Range: bytes=start-end`（只支持单区间即可），`end` 缺省取 `size-1`。
- 非法/不可满足的范围返回 `416` 并带 `Content-Range: bytes */{size}`。
- `Content-Type` 按扩展名映射：`.jpg/.jpeg → image/jpeg`、`.png → image/png`、
  `.webm → video/webm`、`.mp4 → video/mp4`、`.jxr → image/jxr`。
- 用固定大小缓冲（如 64 KB）循环 `read` + `send`，**不要**把整个文件读进内存——
  录屏动辄几十上百 MB。

### 4.6 安全：路径校验（必做）

所有对外暴露的媒体访问，都要经过同一道校验：

```c
/* 只允许索引内的条目；并且再次确认路径落在允许的根目录下。 */
static int media_path_allowed(const char *path) {
    if (!path || strncmp(path, MEDIA_ROOT "/", strlen(MEDIA_ROOT) + 1) != 0)
        return 0;
    if (strstr(path, "/../") || strstr(path, "/./")) return 0;
    return 1;
}
```

- **ID → 路径的映射由服务端索引完成**，绝不接受客户端直接传路径（若为调试方便保留
  `?path=` 形式，必须先做上面的校验，且默认编译期关闭）。
- 拒绝 `..`、符号链接逃逸（`realpath` 后再校验前缀）。
- 不做任何写操作，因此不提供 `PUT` / `DELETE`。
- 配置页里给一个「仅允许局域网访问」开关（绑定到具体网卡或维持 `0.0.0.0` + 提示）。

### 4.7 CORS

如果客户端是**本地打开的 HTML 文件**（`file://`），浏览器会因跨源拒绝请求。
两个办法，任选其一：

- 服务端统一加 `Access-Control-Allow-Origin: *`（只读接口，风险可控）；
- 或者把客户端页面也由本插件托管（`GET /` 返回一个静态页）。

推荐前者，实现最简单。注意 `<video>` 的 Range 请求也会带上 `Origin`，
预检（`OPTIONS`）要正确响应。

---

## 5. 插件侧实现要点

### 5.1 模块划分（照 ftpsrv 插件的成熟范式）

```text
include/
├── plugin_config.h.in     生成的元数据 + 默认端口 + 日志/配置路径
├── plugin_settings.h      配置结构体与读写
├── plugin_ui.h            配置页节点与事件解码
├── media_index.h          媒体索引：扫描、查询、ID 映射
└── http_service.h         HTTP 服务生命周期（init/start/stop/reconfigure/destroy）

source/
├── main.c                 进程生命周期 + UI 事件循环
├── plugin_descriptor.c    内嵌 ELF 描述符
├── plugin_settings.c      .ini 读写（原子写）
├── plugin_ui.c            配置页
├── media_index.c          目录遍历、.meta 解析、索引
├── media_meta.c           容错 JSON 取值
├── http_service.c         监听线程生命周期
├── http_server.c          连接处理、路由、Range
└── http_json.c            JSON 输出辅助
```

这个划分直接对应 `etaHEN-ftpsrv-plugin`：`main.c` 管生命周期，
`*_service.c` 管网络线程的启停，`plugin_ui.c` 只管配置页。

### 5.2 线程模型

**HTTP 服务跑在独立线程，主线程继续以 100 ms 周期轮询 UI 事件。**
这与 ftpsrv 完全一致：

```c
static void *http_thread_main(void *argument) {
    http_service *service = (http_service *)argument;
    pthread_mutex_lock(&service->mutex);
    const uint16_t port = service->port;
    pthread_mutex_unlock(&service->mutex);

    const int result = http_serve(port);        /* 阻塞：accept 循环 */

    pthread_mutex_lock(&service->mutex);
    service->running = 0;
    pthread_mutex_unlock(&service->mutex);
    return NULL;
}
```

共享数据（端口、索引指针、`enabled`）用 `pthread_mutex_t` 保护。
索引被 HTTP 线程读、被 `rescan` 写时，用**读写锁或双缓冲 + 指针原子切换**，
避免长时间持锁阻塞请求。

启动就绪判定照抄 ftpsrv 的 `wait_for_listener()`：轮询一个
`http_server_is_listening()` 标志，最多等 2 秒，失败则回滚并关闭开关。

### 5.3 描述符

```c
ETA_PLUGIN_DEFINE(
    PLUGIN_ID, PLUGIN_VERSION, PLUGIN_NAME,
    ETA_PLUGIN_CAP_IPC | ETA_PLUGIN_CAP_PROCESS | ETA_PLUGIN_CAP_KERNEL |
        ETA_PLUGIN_CAP_UI,
    ETA_PLUGIN_FLAG_LONG_RUNNING | ETA_PLUGIN_FLAG_STOP_SUPPORTED);
```

- `CAP_KERNEL`：提权读 `/user` 所必需。
- `CAP_PROCESS`：与 ftpsrv 一致；若不需要进程操作可去掉。
- `CAP_UI`：配置页。
- 不加 `CAP_NOTIFY` / `CAP_INJECT`：用不到就别申请（README 明确要求只申请实际使用的能力）。
- `LONG_RUNNING` + `STOP_SUPPORTED`：常驻服务，支持优雅停止。

### 5.4 配置页（XML UI，仅设置）

扁平单页即可，节点预算（256）绰绰有余：

| 节点 | 类型 | 说明 |
| --- | --- | --- |
| `main` | `PAGE` | 根页 |
| `status` | `LABEL` | 显示 `已索引 N 条 / 权限正常` 等状态 |
| `enabled` | `TOGGLE` | 服务开关（`BOOL` + `EVENT`） |
| `port` | `INPUT` | 端口（`INT`，`1..65535`，`max_length=5`） |
| `lan_only` | `TOGGLE` | 仅局域网 |
| `rescan` | `ACTION` | 立即重新索引（可加 `CONFIRM`） |

**节点规则**（SDK 硬校验，写错会 `ETA_E_INVALID_ARGUMENT`）：

- `id` / `binding_key` 只允许 `[A-Za-z0-9_.-]`，**不能有斜杠、空格**；
- `PAGE` 不能有父节点；`LABEL` / `ACTION` 等的父节点只能是 `PAGE` 或 `GROUP`；
- `LABEL` / `PAGE` / `MENU` / `GROUP` 的 `value_type` 必须是 `NONE` 且 `binding` 必须是 `NONE`
  （即 `LABEL` 不可点击）；
- `ACTION` 必须 `binding = EVENT` + 非空 `binding_key` + `value_type = NONE`；
- `TOGGLE` 只能 `BOOL`；`INPUT` 只能 `INT` 或 `STRING`。

状态更新用 `eta_ui_set_value()` 增量改；**要改结构（增删节点）必须
`eta_ui_unregister` → `destroy` → 重建 → `register`**，SDK 没有原地更新节点的接口。
ftpsrv 就是这么做的（例如系统语言变化时整份重建）。

### 5.5 配置持久化

照抄 ftpsrv 的 `.ini` 方案，**临时文件 + `fsync` + `rename`** 保证原子性：

```text
/data/etaHEN/plugins/MEDI00001.ini

enabled=true
port=8380
lan_only=false
```

`/data` 是可写且持久的分区，`/user` 不可写。日志写 `/data/etaHEN/MEDI00001.log`。

### 5.6 其它必备细节

- `signal(SIGPIPE, SIG_IGN);` —— 客户端提前断开时，往 socket 写会触发 SIGPIPE 直接杀进程。
- `syscall(SYS_thr_set_name, -1, "MEDI00001.elf");` —— 便于在进程列表里辨认。
- `SO_REUSEADDR` 必须设置，否则重启后端口可能被 TIME_WAIT 占住。
- 设置 `SO_RCVTIMEO` / `SO_SNDTIMEO`，避免慢客户端永久占用线程。
- 构建：`LIBRARIES SceLibcInternal kernel_sys`，必要时补 `SceSystemService`。
- 输出名：`set_target_properties(... OUTPUT_NAME "${ETA_PLUGIN_ID}.elf")`。
- 插件 ID 需 4 字母 + 5 数字（如 `MEDI00001`），版本 `N.NN`，发布后不可改。

---

## 6. 与 Porkfolio（FTP 方案）的能力对照

| 能力 | Porkfolio 做法 | 本插件做法 |
| --- | --- | --- |
| 连接方式 | PC 端主动 FTP 连 PS5 | 插件常驻 HTTP 服务，客户端拉取 |
| 目录遍历 | 递归 `LIST`（深度上限 6） | 递归 `opendir`（深度上限 6） |
| 权限 | 依赖 FTP 服务已提权 | 插件自己 `kernel_set_ucred_authid` |
| 拍摄时间 | 文件名正则（FTP mtime 不可靠） | `.meta` → 文件名 → `stat` 三级回退 |
| 游戏 ID | 路径段正则匹配（**会把 AppId 误判为 GameId**） | 按层级位置取第二段，无误判 |
| 截图预览 | 整文件 base64 成 data URL | HTTP 直出本体 |
| 视频缩略图 | 下 4–10 MB + ffmpeg 抽帧 | 读 `.jxr` 旁挂文件 |
| 视频播放 | 起本地 HTTP 服务 + Range 206 | 插件本身即 HTTP + Range |
| 依赖 | `basic-ftp`、`ffmpeg-static`、Electron | 仅 PS5 payload SDK + libc + pthread |
| 需要 PC | 是 | 否（任意浏览器即可） |

**简化的本质**：Porkfolio 在 PC 端重建了一套「本地 HTTP 服务 + 解码器」，
因为它隔着 FTP；本插件把 HTTP 服务搬到了 PS5 本机，于是 FTP、ffmpeg、
base64 中转、`pork-cache://` 协议全部消失。

---

## 7. 落地建议

### 7.0 实现状态

第一阶段已实现，并在第一、二阶段各顺带完成了一项：

| 项 | 状态 | 位置 |
| --- | --- | --- |
| 提权 + 遍历 `/user/av_contents` + 内存索引 | 已完成 | `source/media_index.c`、`source/main.c` |
| `GET /api/v1/health` | 已完成 | `source/http_server.c` |
| `GET /api/v1/media`（筛选 / 分页 / 排序） | 已完成 | `source/http_server.c` |
| `GET /api/v1/file/{id}`（含 Range） | 已完成 | `source/http_server.c` |
| 配置页：开关 + 端口 + 重扫 | 已完成（另加 loopback 开关） | `source/plugin_ui.c` |
| `.meta` 解析 | 已完成，键名为候选表 + 回退 | `source/media_meta.c` |
| `GET /api/v1/thumb/{id}` | 已完成（原样透出 `.jxr`，见 §4.6） | `source/http_server.c` |
| CORS | 已完成 | `source/http_server.c` |
| `GET /api/v1/games` 聚合 | 未实现 | — |
| `GET /api/v1/debug/meta/{id}` | 未实现 | — |
| 静态页托管（`GET /` 之外的页面） | 未实现 | — |

平台无关部分（扫描、旁挂解析、Range 与查询串解析）有宿主机测试覆盖，
`tests/run_host_smoke.sh` 共 155 条断言，无需实机即可运行。

### 7.1 分阶段

**第一阶段（可验证的最小闭环）**

1. 提权 + 遍历 `/user/av_contents` + 内存索引；
2. `GET /api/v1/health`、`GET /api/v1/media`、`GET /api/v1/file/{id}`（含 Range）；
3. 配置页：开关 + 端口 + 重扫。

到这一步，PC 浏览器已经能列出并**播放**PS5 上的录屏。

**第二阶段**

4. `.meta` 解析（先 dump 真机 `.meta` 确认键名）；
5. `/api/v1/games` 聚合、筛选与分页；
6. `/api/v1/thumb/{id}`（截图直出、视频透出 `.jxr`）；
7. CORS 与静态页托管。

**第三阶段（可选）**

8. 导出：把选中的媒体复制到 `/data/etaHEN/media/` 或 USB（`/mnt/usb0`）；
9. 客户端页面（独立于本仓库）。

### 7.2 明确不做

- 不写、不改、不删 `/user` 下的任何内容；
- 不在插件内做图片缩放或视频转码；
- 不通过 HTTP 暴露 `/user` 之外的路径；
- 不实现认证（局域网自用，与 etaHEN 现有安全模型一致；若需外网访问，应走反向代理加认证）。

### 7.3 真机验证清单

- [ ] 提权后 `opendir("/user/av_contents")` 是否成功？失败时的 errno？
- [ ] 实际层级是否确为 `{AppId}/{GameId}/{hash}/`？有没有固件差异？
- [ ] `{AppId}` 是否恒存在？取第二段作 `game_id` 是否总是对的？
- [ ] dump 一个 `.meta`，确认实际键名，回填 §3.2 候选表；
- [ ] `.jxr` 能否被目标客户端渲染？不能则确认降级为占位图；
- [ ] 录屏实际编码与容器（WebM/VP9？）能否被浏览器直接播放？
- [ ] 万级媒体量下，单次全量遍历耗时与内存占用；
- [ ] 慢客户端/断连场景下，服务线程是否被拖死（SIGPIPE、超时是否生效）。

---

## 8. 参考来源

- 本仓库 `etaHEN-plugin-sdk`：`include/eta/ui.h`（节点类型与容量上限）、
  `runtime/ui.c`（节点校验规则）、`docs/architecture.md`（UI 贡献与生命周期边界）。
- 同级仓库 `etaHEN-ftpsrv-plugin`：`source/main.c`（提权与生命周期）、
  `source/ftp_service.c`（网络线程范式）、`source/plugin_settings.c`（原子配置写）。
- Porkfolio（`/Users/chenpy/Projects/Person/PS5/Others/Porkfolio`）：
  `src/ipc/media.js`（FTP 目录约定、时间戳解析、WebM 修复）、
  `docs/obsidian/Porkfolio/Media.md`。
- 第三方交叉验证：airpsx `MediaListHandler`（`/user/av_contents` 递归扫描、
  `.meta` / `.jxr` 处理）、ConsoleMods Wiki「Files and Directories」。

/* Copyright (C) 2025 etaHEN / LightningMods
 *
 * Strict user Payload ELF loader (PID files + private :9020 elfldr).
 */

#include <eta/payload.h>
#include <eta/elf_name.h>

#include <elfldr_remote.h>
#include <eta/log.h>
#include <eta/notify.h>
#include <eta/proc_query.h>
#include <eta/system_tmp.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int sceKernelGetProcessName(int pid, char *name);

bool eta_payload_is_elf(const void *buf, size_t size) {
  if (!buf || size < 4)
    return false;
  static const unsigned char kMagic[] = {0x7F, 'E', 'L', 'F'};
  return memcmp(buf, kMagic, 4) == 0;
}

bool eta_payload_elf_key_from_name(const char *name, char *out, size_t out_sz) {
  if (!name || !out || out_sz < 2)
    return false;

  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  if (!base[0] || strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
    return false;

  size_t n = eta_elf_name_stem_n(base, strlen(base));
  if (n == 0)
    return false; /* bare ".elf" */
  if (n >= out_sz)
    n = out_sz - 1;

  memcpy(out, base, n);
  out[n] = '\0';
  if (out[0] == '\0' || strchr(out, '/') != NULL || strchr(out, '\\') != NULL)
    return false;
  return true;
}

void eta_payload_pid_path(char *out, size_t out_sz, const char *title_id) {
  (void)eta_system_tmp_pid_path(out, out_sz, title_id);
}

pid_t eta_payload_read_pid_file(const char *pid_path) {
  const int f = open(pid_path, O_RDONLY);
  if (f < 0)
    return -1;
  char t[32];
  const int r = (int)read(f, t, sizeof(t) - 1);
  close(f);
  if (r <= 0)
    return -1;
  t[r] = '\0';
  return (pid_t)atoi(t);
}

void eta_payload_write_pid_file(const char *pid_path, pid_t pid) {
  if (!pid_path) {
    return;
  }
  if (pid <= 1) {
    unlink(pid_path);
    return;
  }

  mkdir(ETA_SYSTEM_TMP_ROOT, 0777);
  mkdir(ETA_SYSTEM_TMP_PID_ROOT, 0777);
  const int f = open(pid_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (f < 0)
    return;
  char t[32];
  const int len = snprintf(t, sizeof(t), "%d", pid);
  (void)write(f, t, (size_t)len);
  close(f);
}

bool eta_payload_running(const char *title_id) {
  if (!title_id || !title_id[0])
    return false;
  char path[256];
  eta_payload_pid_path(path, sizeof(path), title_id);
  const pid_t pid = eta_payload_read_pid_file(path);
  if (pid <= 1) {
    if (pid == 1) {
      LOG_WARN("Ignoring bogus payload PID 1 for %s", title_id);
      unlink(path);
    }
    return false;
  }
  if (!eta_proc_is_alive(pid)) {
    LOG_WARN("Stale payload PID file for %s (pid=%d dead), removing",
             title_id, (int)pid);
    unlink(path);
    return false;
  }
  return true;
}

static size_t payload_pid_paths(const char *path, char paths[2][256]) {
  char identity[ETA_PAYLOAD_IDENTITY_SIZE], basename_key[64];
  if (!eta_payload_identity_from_path(path, identity, sizeof(identity)) ||
      !eta_payload_elf_key_from_name(path, basename_key, sizeof(basename_key)))
    return 0;
  eta_payload_pid_path(paths[0], 256, identity);
  if (strcmp(identity, basename_key) == 0)
    return 1;
  eta_payload_pid_path(paths[1], 256, basename_key);
  return 2;
}

bool eta_payload_path_running(const char *path) {
  char paths[2][256];
  const size_t count = payload_pid_paths(path, paths);
  for (size_t i = 0; i < count; ++i) {
    const pid_t pid = eta_payload_read_pid_file(paths[i]);
    if (pid > 1 && eta_proc_is_alive(pid))
      return true;
  }
  return false;
}

bool eta_payload_remove(const char *path) {
  char pid_paths[2][256];
  const char *base = path ? strrchr(path, '/') : NULL;
  if (!base || path[0] != '/' || strlen(path) >= ETA_PAYLOAD_PATH_SIZE ||
      strlen(base + 1) <= ETA_ELF_SUFFIX_LEN ||
      !eta_elf_name_has_suffix(base + 1, strlen(base + 1)) ||
      strchr(base + 1, '\\') || strstr(path, "/../") || strstr(path, "/./") ||
      strstr(path, "//")) {
    errno = EINVAL;
    return false;
  }
  struct stat st;
  if (lstat(path, &st) != 0)
    return false;
  if (!S_ISREG(st.st_mode)) {
    errno = EINVAL;
    return false;
  }
  const size_t pid_count = payload_pid_paths(path, pid_paths);
  if (!pid_count) {
    errno = EINVAL;
    return false;
  }
  for (size_t i = 0; i < pid_count; ++i) {
    const pid_t pid = eta_payload_read_pid_file(pid_paths[i]);
    if (pid > 1 && eta_proc_is_alive(pid)) {
      errno = EBUSY;
      return false;
    }
  }
  if (unlink(path) != 0)
    return false;

  int error = 0;
  const char *suffixes[] = {".auto_start", ".json"};
  for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
    char sidecar[ETA_PAYLOAD_PATH_SIZE + 12];
    snprintf(sidecar, sizeof(sidecar), "%s%s", path, suffixes[i]);
    // A valid ELF basename may leave no room for a sidecar suffix.
    if (unlink(sidecar) != 0 && errno != ENOENT && errno != ENAMETOOLONG && !error)
      error = errno;
  }
  for (size_t i = 0; i < pid_count; ++i)
    if (unlink(pid_paths[i]) != 0 && errno != ENOENT && !error)
      error = errno;
  errno = error;
  return error == 0;
}

static bool valid_payload_key(const char *title_id) {
  return title_id && title_id[0] && strcmp(title_id, ".") != 0 &&
         strcmp(title_id, "..") != 0 && strlen(title_id) < 64 &&
         strchr(title_id, '/') == NULL && strchr(title_id, '\\') == NULL;
}

static pid_t launch_user_payload(const char *title_id, const char *abs_path) {
  if (!elfldr_remote_eta_available()) {
    LOG_ERROR("Private elfldr :%u unavailable; launch failed for %s",
              ETA_ELFLDR_PORT, title_id);
    return -1;
  }

  const pid_t pid = elfldr_remote_eta_launch_file_get_pid(abs_path, NULL);
  if (pid <= 1) {
    LOG_ERROR("Private elfldr :%u returned no valid PID for %s (pid=%d)",
              ETA_ELFLDR_PORT, title_id, (int)pid);
    return -1;
  }

  char pname[32] = {0};
  if (sceKernelGetProcessName(pid, pname) == 0)
    LOG_INFO("Launched via %u (pid=%d name=%s)", ETA_ELFLDR_PORT, (int)pid,
             pname);
  else
    LOG_INFO("Launched via %u (pid=%d)", ETA_ELFLDR_PORT, (int)pid);
  return pid;
}

pid_t eta_payload_launch_elfldr(const char *title_id, const char *path) {
  if (!valid_payload_key(title_id) || !path || path[0] != '/') {
    LOG_ERROR("launch_elfldr: invalid args title=%s path=%s",
              title_id ? title_id : "(null)", path ? path : "(null)");
    return -1;
  }

  LOG_INFO("loading payload via elfldr key=%s path=%s", title_id, path);
  return launch_user_payload(title_id, path);
}

uint8_t *eta_payload_read_file(const char *path, size_t *out_size) {
  if (!path || !out_size)
    return NULL;

  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("Failed to open file %s (%s)", path, strerror(errno));
    return NULL;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    LOG_ERROR("Failed to stat file %s", path);
    close(fd);
    return NULL;
  }
  if (st.st_size <= 0) {
    LOG_WARN("Empty payload file %s", path);
    close(fd);
    return NULL;
  }

  uint8_t *buf = (uint8_t *)malloc((size_t)st.st_size);
  if (!buf) {
    LOG_ERROR("Failed to allocate %lld bytes for payload",
                 (long long)st.st_size);
    close(fd);
    return NULL;
  }

  if (read(fd, buf, (size_t)st.st_size) != st.st_size) {
    LOG_ERROR("Failed to read payload file %s", path);
    free(buf);
    close(fd);
    return NULL;
  }
  close(fd);
  *out_size = (size_t)st.st_size;
  return buf;
}

bool eta_payload_load_with_key(const char *path, const char *filename,
                                 const char *requested_key) {
  size_t size = 0;
  uint8_t *buf = eta_payload_read_file(path, &size);
  if (!buf)
    return false;

  char name_buf[256];
  const char *base = filename;
  if (!base || !base[0]) {
    snprintf(name_buf, sizeof(name_buf), "%s", path);
    base = basename(name_buf);
  }

  const size_t base_len = strlen(base);
  if (!eta_elf_name_has_suffix(base, base_len) ||
      eta_elf_name_stem_n(base, base_len) == 0) {
    LOG_WARN("Not a .elf payload: %s", base);
    eta_notify(1, "notify.payload.elf_only", base);
    free(buf);
    return false;
  }

  if (!eta_payload_is_elf(buf, size)) {
    LOG_ERROR("Invalid ELF file: %s", base);
    eta_notify(1, "notify.payload.invalid_elf", base);
    free(buf);
    return false;
  }

  char key[64];
  if (requested_key && valid_payload_key(requested_key)) {
    snprintf(key, sizeof(key), "%s", requested_key);
  } else if (!eta_payload_elf_key_from_name(base, key, sizeof(key))) {
    LOG_ERROR("Invalid ELF basename (empty stem): %s", base);
    eta_notify(1, "notify.payload.invalid_name", base);
    free(buf);
    return false;
  }

  LOG_DEBUG("payload launch key=%s (from %s)", key, base);
  char pid_path[256];
  eta_payload_pid_path(pid_path, sizeof(pid_path), key);

  if (eta_payload_running(key)) {
    LOG_INFO("Payload %s is already running; keeping the recorded instance",
             key);
    free(buf);
    return true;
  }

  if (!elfldr_remote_eta_available()) {
    LOG_WARN("Private elfldr :%u unavailable; payload %s not started",
             ETA_ELFLDR_PORT, key);
    free(buf);
    return false;
  }

  const pid_t pid = eta_payload_launch_elfldr(key, path);
  free(buf);
  /* Only persist real pids; never write 0/1 (PID 1 would hit system init). */
  if (pid > 1)
    eta_payload_write_pid_file(pid_path, pid);
  else
    eta_payload_write_pid_file(pid_path, -1);
  return pid > 1;
}

bool eta_payload_load(const char *path, const char *filename) {
  return eta_payload_load_with_key(path, filename, NULL);
}

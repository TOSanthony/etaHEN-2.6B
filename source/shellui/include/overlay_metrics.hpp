#pragma once

#include <eta/settings.hpp>

#include <cstddef>
#include <string_view>

namespace eta::overlay {

struct MetricUi {
  int metric;
  const char *name;
  const char *title_key;
  const char *desc_key;
  const char *enable_id;
  const char *order_id;
};

inline constexpr MetricUi kMetricUi[] = {
    {eta::kOverlayMetricFps, "fps", "overlay.fps", "overlay.fps.desc",
     "id_overlay_fps", "id_overlay_fps_order"},
    {eta::kOverlayMetricCpu, "cpu", "overlay.cpu", "overlay.cpu.desc",
     "id_overlay_cpu", "id_overlay_cpu_order"},
    {eta::kOverlayMetricGpu, "gpu", "overlay.gpu", "overlay.gpu.desc",
     "id_overlay_gpu", "id_overlay_gpu_order"},
    {eta::kOverlayMetricMemory, "memory", "overlay.ram", "overlay.ram.desc",
     "id_overlay_ram", "id_overlay_ram_order"},
    {eta::kOverlayMetricIp, "ip", "overlay.ip", "overlay.ip.desc",
     "id_overlay_ip", "id_overlay_ip_order"},
    {eta::kOverlayMetricFan, "fan", "overlay.fan", "overlay.fan.desc",
     "id_overlay_fan", "id_overlay_fan_order"},
};

inline constexpr std::size_t kMetricUiCount =
    sizeof(kMetricUi) / sizeof(kMetricUi[0]);

inline const MetricUi *metric_ui_by_metric(int metric) {
  for (const auto &row : kMetricUi) {
    if (row.metric == metric)
      return &row;
  }
  return nullptr;
}

inline const MetricUi *metric_ui_by_name(std::string_view name) {
  for (const auto &row : kMetricUi) {
    if (name == row.name)
      return &row;
  }
  return nullptr;
}

inline bool metric_toggle_on(const eta::Settings &s, int metric) {
  switch (metric) {
  case eta::kOverlayMetricFps:
    return s.overlay_fps;
  case eta::kOverlayMetricCpu:
    return s.overlay_cpu;
  case eta::kOverlayMetricGpu:
    return s.overlay_gpu;
  case eta::kOverlayMetricMemory:
    return s.overlay_ram;
  case eta::kOverlayMetricIp:
    return s.overlay_ip;
  case eta::kOverlayMetricFan:
    return s.overlay_fan;
  default:
    return false;
  }
}

} // namespace eta::overlay

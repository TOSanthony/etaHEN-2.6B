/* Copyright (C) 2026 etaHEN / LightningMods */
#pragma once

#include <eta/fps_sample.h>

namespace eta {
namespace fps {

bool publish_open();
void publish_close();
void publish(const EtaFpsSample &sample);

} // namespace fps
} // namespace eta

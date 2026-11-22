#pragma once

#include <eta/plugin_session.hpp>
#include <eta/plugin_ui.hpp>

namespace eta::daemon::plugin_ui_bridge {

bool start(plugin_ui::ProtocolBroker &broker, plugin_ui::Registry &registry);
void stop();
void restart();
bool is_listening();
plugin_session::EventSource &event_source();

} // namespace eta::daemon::plugin_ui_bridge

#pragma once

#include "eta/client.h"
#include "eta/ui.h"

eta_status eta_ui_client_make_services(eta_client *client,
                                           eta_ui_services_v1 *out_services);


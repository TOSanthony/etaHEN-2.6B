add_library(eta_sdk_runtime STATIC
    host_api.c
    status.c
    transport.c
    services.c
    runtime.c
    event_bus.c
    client.c
    ui.c
    ui_client.c
    ui_event.c)
add_library(etaHEN::Runtime ALIAS eta_sdk_runtime)
set_target_properties(eta_sdk_runtime PROPERTIES EXPORT_NAME Runtime)
target_link_libraries(eta_sdk_runtime PUBLIC etaHEN::SDK)
target_link_libraries(eta_sdk_runtime PRIVATE Threads::Threads)
target_compile_features(eta_sdk_runtime PRIVATE c_std_11)

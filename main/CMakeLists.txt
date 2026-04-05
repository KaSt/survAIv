set(_requires app_update esp_event esp_http_client esp_http_server esp_netif esp_timer esp_wifi json mbedtls nvs_flash)

if(SURVAIV_HAS_DISPLAY OR DEFINED ENV{SURVAIV_DISPLAY})
    list(APPEND _requires screen)
endif()

idf_component_register(
    SRCS "main.cpp"
         "json_util.cpp"
         "http.cpp"
         "wifi.cpp"
         "polymarket.cpp"
         "ledger.cpp"
         "agent.cpp"
         "crypto.cpp"
         "wallet.cpp"
         "clob.cpp"
         "config.cpp"
         "dashboard_state.cpp"
         "webserver.cpp"
         "onboard.cpp"
         "x402.cpp"
         "model_registry.cpp"
          "claw402.cpp"
          "provider.cpp"
         "wisdom.cpp"
         "news.cpp"
         "telemetry.cpp"
    INCLUDE_DIRS "."
    REQUIRES ${_requires}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE SURVAIV_TIER="${SURVAIV_TIER}")

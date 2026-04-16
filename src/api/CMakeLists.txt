# Copyright (c) 2023-2026 The Dogecoin developers

add_library(api
    api_server.cpp
    api_util.cpp
    chain_handler.cpp
    dashboard_handler.cpp
    network_handler.cpp
    mining_handler.cpp
    events_handler.cpp
    stratum_handler.cpp
)

target_include_directories(api PUBLIC ${CMAKE_SOURCE_DIR}/src)

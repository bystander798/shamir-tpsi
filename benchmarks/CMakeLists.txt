add_executable(tpsi_bench
    protocol_bench.cpp
)

target_link_libraries(tpsi_bench PRIVATE tpsi)

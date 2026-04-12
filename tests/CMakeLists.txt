add_executable(tpsi_tests
    shamir_tests.cpp
)

target_link_libraries(tpsi_tests PRIVATE tpsi)

add_test(NAME tpsi_tests COMMAND tpsi_tests)

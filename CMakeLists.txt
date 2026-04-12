cmake_minimum_required(VERSION 3.16)
project(shair_tpsi LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(OpenSSL REQUIRED)
find_package(NTL REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SHAIR_TPSI_BUILD_TESTS "Build unit tests" ON)
option(SHAIR_TPSI_BUILD_BENCHMARKS "Build benchmarks" ON)
option(SHAIR_TPSI_USE_FLINT "Enable FLINT-accelerated polynomial arithmetic" ON)
option(SHAIR_TPSI_BUILD_SERVER "Build HTTP API server" ON)

if(SHAIR_TPSI_USE_FLINT)
    find_package(FLINT QUIET)
    if(FLINT_FOUND)
        message(STATUS "FLINT found: enabling accelerated RSS backend")
        set(SHAIR_TPSI_HAVE_FLINT ON)
    else()
        message(STATUS "FLINT not found, falling back to NTL-based polynomial backend")
        set(SHAIR_TPSI_HAVE_FLINT OFF)
    endif()
else()
    set(SHAIR_TPSI_HAVE_FLINT OFF)
endif()

add_library(tpsi STATIC
    src/ring.cpp
    src/shamir.cpp
    src/ring_lpn_pcg.cpp
    src/rss.cpp
    src/polynomial.cpp
    src/polynomial_backend.cpp
    src/session.cpp
    src/message_channel.cpp
    src/serialization.cpp
    src/config_io.cpp
    src/aes_ctr_prng.cpp
    src/coin_flip.cpp
    src/random_provider.cpp
    src/pcg.cpp
)

target_include_directories(tpsi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tpsi
    PUBLIC
        OpenSSL::Crypto
        NTL::ntl
)

add_executable(tpsi_receiver apps/receiver_main.cpp)
target_link_libraries(tpsi_receiver PRIVATE tpsi)

add_executable(tpsi_sender apps/sender_main.cpp)
target_link_libraries(tpsi_sender PRIVATE tpsi)

if(SHAIR_TPSI_HAVE_FLINT)
    target_link_libraries(tpsi PUBLIC FLINT::flint)
    target_compile_definitions(tpsi PUBLIC SHAIR_TPSI_HAVE_FLINT=1)
else()
    target_compile_definitions(tpsi PUBLIC SHAIR_TPSI_HAVE_FLINT=0)
endif()

if(SHAIR_TPSI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SHAIR_TPSI_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(SHAIR_TPSI_BUILD_SERVER)
    add_executable(tpsi_server
        src/server/main.cpp
    )

    target_include_directories(tpsi_server
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    )

    target_link_libraries(tpsi_server
        PRIVATE
            tpsi
            OpenSSL::Crypto
            NTL::ntl
    )
endif()

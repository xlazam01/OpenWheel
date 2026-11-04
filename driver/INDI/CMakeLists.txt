cmake_minimum_required(VERSION 3.16)
project(indi_openwheel CXX)

# INDI driver for the OpenWheel filter wheel. Laid out like an indi-3rdparty driver so it can be
# submitted upstream as indi-openwheel later.
set(OPENWHEEL_VERSION_MAJOR 1)
set(OPENWHEEL_VERSION_MINOR 0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra)

include(GNUInstallDirs)

option(OPENWHEEL_BUILD_DRIVER "Build the INDI driver (needs libindi >= 2.0)" ON)
option(OPENWHEEL_BUILD_TESTS "Build the tests" ON)
option(OPENWHEEL_INSTALL_UDEV_RULES "Install the udev rule" ON)
set(UDEVRULES_INSTALL_DIR "/lib/udev/rules.d" CACHE STRING "udev rules directory")

# Protocol, serial link, detection and the wheel session: no libindi dependency.
add_library(openwheel_core STATIC src/protocol.cpp src/serial_link.cpp src/detect.cpp src/wheel.cpp)
target_include_directories(openwheel_core PUBLIC src)
set_target_properties(openwheel_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Simulated wheel on a pty, for tests and for trying the driver without hardware.
find_library(UTIL_LIBRARY util)
find_package(Threads REQUIRED)
add_library(openwheel_simlib STATIC sim/sim_wheel.cpp)
target_include_directories(openwheel_simlib PUBLIC sim)
target_link_libraries(openwheel_simlib PUBLIC ${UTIL_LIBRARY} Threads::Threads)
add_executable(openwheel_sim sim/sim_main.cpp)
target_link_libraries(openwheel_sim openwheel_simlib)
install(TARGETS openwheel_sim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(OPENWHEEL_BUILD_DRIVER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(INDI REQUIRED libindi>=2.0)
    find_library(INDI_DRIVER_LIBRARY indidriver HINTS ${INDI_LIBRARY_DIRS} REQUIRED)
    find_package(ZLIB REQUIRED)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/indi_openwheel.xml.cmake ${CMAKE_CURRENT_BINARY_DIR}/indi_openwheel.xml)

    add_executable(indi_openwheel src/indi_openwheel.cpp)
    target_include_directories(indi_openwheel PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${INDI_INCLUDE_DIRS})
    target_link_libraries(indi_openwheel openwheel_core ${INDI_DRIVER_LIBRARY} ZLIB::ZLIB Threads::Threads)

    install(TARGETS indi_openwheel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/indi_openwheel.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/indi)
endif()

if(OPENWHEEL_INSTALL_UDEV_RULES)
    install(FILES 99-openwheel.rules DESTINATION ${UDEVRULES_INSTALL_DIR})
endif()

if(OPENWHEEL_BUILD_TESTS)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core openwheel_core openwheel_simlib)
    add_test(NAME core COMMAND test_core)

    if(OPENWHEEL_BUILD_DRIVER)
        find_program(INDISERVER indiserver)
        if(INDISERVER)
            add_test(NAME indiserver
                     COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_indiserver.sh
                             $<TARGET_FILE:indi_openwheel> $<TARGET_FILE:openwheel_sim>)
            set_tests_properties(indiserver PROPERTIES TIMEOUT 180)
        else()
            message(STATUS "indiserver not found: the end-to-end test is skipped")
        endif()
    endif()
endif()

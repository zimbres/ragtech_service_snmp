cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)
project(ragtech_service VERSION 3.0.0 LANGUAGES C)
if(WIN32 AND CMAKE_VERSION VERSION_LESS "3.21")
    message(FATAL_ERROR "The Windows C11 build requires CMake 3.21 or newer")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Native platform support; Windows only links system libraries.
if(WIN32)
    add_library(ragtech_platform STATIC src/platform_windows.c)
    target_compile_definitions(ragtech_platform PUBLIC
        _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(ragtech_platform PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    add_library(ragtech_platform STATIC src/platform_posix.c)
    target_link_libraries(ragtech_platform PUBLIC Threads::Threads m)
endif()
target_include_directories(ragtech_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Static MSVC runtime produces an executable without a VC redistributable installer.
if(MSVC)
    set_property(TARGET ragtech_platform PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Source files
set(SRCS
    src/main.c
    src/ups.c
    src/family10.c
    src/ups_snmp.c
    src/config.c
    src/log.c
)

add_executable(${PROJECT_NAME} ${SRCS})

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wformat=2
        -Wno-unused-parameter -Wno-missing-field-initializers
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE ragtech_platform)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Install target
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

if(WIN32)
    install(FILES windows/ragtech.conf DESTINATION bin)
    install(FILES windows/README.md windows/register-startup.ps1 DESTINATION bin)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "ragtech_service")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME "ragtech_service-${PROJECT_VERSION}-windows-${CMAKE_SYSTEM_PROCESSOR}")
    include(CPack)
else()
    install(FILES ragtech.conf DESTINATION etc/ragtech)
    install(FILES ragtech.service DESTINATION lib/systemd/system)
endif()
# Optional monitoring metadata; adds no runtime dependencies.
install(FILES mibs/RAGTECH-UPS-MIB.txt DESTINATION share/snmp/mibs)
install(FILES mibs/README.md DESTINATION share/doc/ragtech_service RENAME MIB-README.md)

install(FILES zabbix/ragtech_ups_snmp.json zabbix/README.md zabbix/install_template.py
    DESTINATION share/ragtech_service/zabbix)

option(BUILD_TESTING "Build protocol tests (requires Python 3)" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(test_platform tests/test_platform.c)
    target_link_libraries(test_platform PRIVATE ragtech_platform)
    if(MSVC)
        set_property(TARGET test_platform PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME platform COMMAND test_platform)
    add_executable(serial_fixture tests/serial_fixture.c src/family10.c)
    target_link_libraries(serial_fixture PRIVATE ragtech_platform)
    if(MSVC)
        set_property(TARGET serial_fixture PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME serial COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_serial.py $<TARGET_FILE:serial_fixture>)
    add_executable(test_family10 tests/test_family10.c src/family10.c)
    target_include_directories(test_family10 PRIVATE include)
    target_link_libraries(test_family10 PRIVATE ragtech_platform)
    if(MSVC)
        set_property(TARGET test_family10 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME family10 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_family10_xml.py $<TARGET_FILE:test_family10>)
    add_executable(test_snmp tests/snmp_fixture.c src/ups_snmp.c src/config.c src/log.c)
    target_include_directories(test_snmp PRIVATE include)
    target_link_libraries(test_snmp PRIVATE ragtech_platform)
    if(MSVC)
        set_property(TARGET test_snmp PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME udp COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_udp.py $<TARGET_FILE:test_snmp>)
    add_test(NAME cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py $<TARGET_FILE:ragtech_service>)
    add_test(NAME zabbix_template COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_zabbix.py $<TARGET_FILE:test_snmp>)
    add_test(NAME snmp COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_snmp.py $<TARGET_FILE:test_snmp>)
endif()

if(BUILD_TESTING)
    set_tests_properties(platform serial family10 udp cli zabbix_template snmp PROPERTIES TIMEOUT 60)
endif()

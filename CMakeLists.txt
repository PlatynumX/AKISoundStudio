cmake_minimum_required(VERSION 3.20)
project(AKISoundStudio VERSION 0.7.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(aki_core
    src/AkiCore.cpp
    src/AkiCore.h
)
target_include_directories(aki_core PUBLIC src)
if(MSVC)
    target_compile_options(aki_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(aki_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(AKISoundStudio WIN32 src/App.cpp)
    target_compile_definitions(AKISoundStudio PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(AKISoundStudio PRIVATE aki_core comctl32 comdlg32 winmm shell32)

    add_custom_command(TARGET AKISoundStudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AKISoundStudio>/data"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/data/vpw2_sounds.csv"
            "${CMAKE_SOURCE_DIR}/data/wm2k_sounds.csv"
            "${CMAKE_SOURCE_DIR}/data/revenge_redux_sounds.csv"
            "${CMAKE_SOURCE_DIR}/data/no_mercy_sounds.csv"
            "$<TARGET_FILE_DIR:AKISoundStudio>/data"
        VERBATIM
    )

    install(TARGETS AKISoundStudio RUNTIME DESTINATION .)
    install(DIRECTORY data DESTINATION .)
endif()

include(CTest)
if(BUILD_TESTING)
    set(AKI_TEST_FIXTURE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/fixtures")
    set(AKI_TEST_FIXTURE_BINARY_DIR "${CMAKE_BINARY_DIR}/tests/fixtures")
    set(AKI_REAL_LOOP_FIXTURE "${AKI_TEST_FIXTURE_SOURCE_DIR}/austin.wav")

    if(NOT EXISTS "${AKI_REAL_LOOP_FIXTURE}")
        message(FATAL_ERROR
            "Required regression fixture is missing: ${AKI_REAL_LOOP_FIXTURE}\n"
            "This is a source-package/test configuration error, not a WAV importer failure.")
    endif()

    file(MAKE_DIRECTORY "${AKI_TEST_FIXTURE_BINARY_DIR}")
    configure_file(
        "${AKI_REAL_LOOP_FIXTURE}"
        "${AKI_TEST_FIXTURE_BINARY_DIR}/austin.wav"
        COPYONLY
    )

    add_executable(aki_core_smoke tests/core_smoke.cpp)
    target_link_libraries(aki_core_smoke PRIVATE aki_core)
    target_compile_definitions(aki_core_smoke PRIVATE
        AKI_TEST_FIXTURE_DIR="${AKI_TEST_FIXTURE_BINARY_DIR}"
    )
    add_test(NAME core_synthetic COMMAND aki_core_smoke)
endif()

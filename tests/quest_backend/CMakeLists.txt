cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32s3)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(quest_backend_tests)

target_link_libraries(${project_elf} PRIVATE
    "-Wl,--wrap=unity_putc"
)

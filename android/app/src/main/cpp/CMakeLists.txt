# libxerabora.so: the upstream PC client, unmodified, plus the Android
# backends in this directory.
#
# Nothing under client/ or third_party/ is edited for Android. The source
# lists are read from client/Makefile, so a file added upstream is built
# here too; the platform differences are replaced backends (the upstream
# pattern of http_curl.c / http_winhttp.c) and two linker wraps.

cmake_minimum_required(VERSION 3.22)
project(xerabora C)

# app/src/main/cpp -> repository root.
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(CLIENT "${ROOT}/client")
set(RC "${ROOT}/third_party/rcheevos")

if(NOT EXISTS "${RC}/include/rc_client.h")
    message(FATAL_ERROR "third_party/rcheevos is empty: run `git submodule update --init third_party/rcheevos`")
endif()

file(READ "${CLIENT}/Makefile" MAKEFILE)
string(REPLACE "\r" "" MAKEFILE "${MAKEFILE}")
string(REGEX REPLACE "\\\\\n" " " MAKEFILE "${MAKEFILE}")

# The words of a `NAME := ...` assignment in client/Makefile.
function(makefile_list name out)
    string(REGEX MATCH "\n${name}[ \t]*:=[^\n]*" line "\n${MAKEFILE}")
    if(line STREQUAL "")
        message(FATAL_ERROR "client/Makefile no longer assigns ${name} :=; "
                            "update android/app/src/main/cpp/CMakeLists.txt")
    endif()
    string(REGEX REPLACE "^\n${name}[ \t]*:=" "" line "${line}")
    string(REPLACE "$(RC)" "${RC}" line "${line}")
    separate_arguments(words UNIX_COMMAND "${line}")
    set(${out} ${words} PARENT_SCOPE)
endfunction()

makefile_list(SRC CLIENT_SRC)
makefile_list(RC_SRC RC_SOURCES)
list(TRANSFORM CLIENT_SRC PREPEND "${CLIENT}/")

# sound.c shells out to paplay/aplay; sound_android.c plays through Java.
list(FILTER CLIENT_SRC EXCLUDE REGEX "/src/sound\\.c$")

add_library(xerabora SHARED
    ${CLIENT_SRC}
    ${RC_SOURCES}
    jni_bridge.c
    http_android.c
    sound_android.c
    hooks.c
    status.c
    net_guard.c)

target_include_directories(xerabora PRIVATE
    "${RC}/include" "${RC}/src" "${RC}/src/rcheevos" "${RC}/src/rapi"
    "${ROOT}/protocol" "${CLIENT}/src")

target_compile_options(xerabora PRIVATE -std=gnu99 -Wall -Wno-unused-parameter)

# The client's main() becomes a function the service thread calls.
set_source_files_properties("${CLIENT}/src/main.c" PROPERTIES COMPILE_DEFINITIONS "main=xerabora_main")

# Defined in webui.c / console.c and called from main.c, ra.c and
# follow.c, so the wrap reaches every call. hooks.c replaces the first
# two; status.c listens to the rest for the notifications. The send
# family is libc's: net_guard.c bounds the page's sends and hands the
# console's datagrams to status.c.
target_link_options(xerabora PRIVATE
    "-Wl,--wrap=webui_page_gone"
    "-Wl,--wrap=webui_open_browser"
    "-Wl,--wrap=webui_set_console"
    "-Wl,--wrap=webui_set_game"
    "-Wl,--wrap=webui_set_game_title"
    "-Wl,--wrap=webui_set_status"
    "-Wl,--wrap=webui_note_unlock"
    "-Wl,--wrap=webui_push"
    "-Wl,--wrap=console_serve"
    "-Wl,--wrap=send"
    "-Wl,--wrap=sendto"
    "-Wl,--wrap=__sendto_chk")

target_link_libraries(xerabora PRIVATE log m)

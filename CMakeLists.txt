cmake_minimum_required(VERSION 3.5)
project(pvr.nextpvr)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)
find_package(ZLIB REQUIRED)
find_package(TinyXML2 REQUIRED)

include_directories(${TINYXML2_INCLUDE_DIR}
                    ${ZLIB_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}/..) # Hack way with "/..", need bigger Kodi cmake rework to match right include ways

set(NEXTPVR_SOURCES src/addon.cpp
                    src/pvrclient-nextpvr.cpp
                    src/Socket.cpp
                    src/uri.cpp
                    src/BackendRequest.cpp
                    src/Channels.cpp
                    src/EPG.cpp
                    src/MenuHook.cpp
                    src/Recordings.cpp
                    src/InstanceSettings.cpp
                    src/Timers.cpp
                    src/buffers/Buffer.cpp
                    src/buffers/DummyBuffer.cpp
                    src/buffers/TranscodedBuffer.cpp
                    src/buffers/ClientTimeshift.cpp
                    src/buffers/RecordingBuffer.cpp
                    src/buffers/CircularBuffer.cpp
                    src/utilities/GenreMapper.cpp
                    src/utilities/SettingsMigration.cpp
                    src/buffers/Seeker.cpp)

set(NEXTPVR_HEADERS src/addon.h
                    src/os-dependent.h
                    src/pvrclient-nextpvr.h
                    src/Socket.h
                    src/uri.h
                    src/BackendRequest.h
                    src/Channels.h
                    src/EPG.h
                    src/MenuHook.h
                    src/Recordings.h
                    src/InstanceSettings.h
                    src/Timers.h
                    src/buffers/Buffer.h
                    src/buffers/DummyBuffer.h
                    src/buffers/TranscodedBuffer.h
                    src/buffers/ClientTimeshift.h
                    src/buffers/RecordingBuffer.h
                    src/buffers/CircularBuffer.h
                    src/buffers/Seeker.h
                    src/utilities/GenreMapper.h
                    src/utilities/SettingsMigration.h
                    src/utilities/XMLUtils.h)

SET(DEPLIBS ${TINYXML2_LIBRARIES}
            ${ZLIB_LIBRARIES})

message(STATUS "ZLIB_LIBRARIES: ${ZLIB_LIBRARIES}")

if(WIN32)
  list(APPEND DEPLIBS ws2_32)
  add_definitions(-D_WINSOCKAPI_ -D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

build_addon(pvr.nextpvr NEXTPVR DEPLIBS)

# Hide all symbols except the Kodi ABI entry points (ADDON_* keep their
# explicit ATTR_DLL_EXPORT visibility). Without this, inline tinyxml2/zlib
# code compiled into the addon is exported and can be interposed by a
# different library version already loaded in Kodi's process.
if(NOT WIN32)
  set_target_properties(pvr.nextpvr PROPERTIES
                        CXX_VISIBILITY_PRESET hidden
                        VISIBILITY_INLINES_HIDDEN ON)
  if(NOT APPLE)
    set_property(TARGET pvr.nextpvr APPEND_STRING PROPERTY
                 LINK_FLAGS " -Wl,--exclude-libs,ALL")
  endif()
endif()

include(CPack)

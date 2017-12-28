project(pvr.nextpvr)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(Kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(p8-platform REQUIRED)
find_package(TinyXML REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${p8-platform_INCLUDE_DIRS}
                    ${TINYXML_INCLUDE_DIR}
                    ${KODI_INCLUDE_DIR})

set(NEXTPVR_SOURCES src/client.cpp
                    src/md5.cpp
                    src/pvrclient-nextpvr.cpp
                    src/Socket.cpp
                    src/uri.cpp
                    src/buffers/Buffer.cpp
                    src/buffers/DummyBuffer.cpp
                    src/buffers/TimeshiftBuffer.cpp
                    src/buffers/RecordingBuffer.cpp
                    src/buffers/CircularBuffer.cpp
                    src/buffers/Seeker.cpp)

set(NEXTPVR_HEADERS src/client.h
                    src/FileUtils.h
                    src/md5.h
                    src/os-dependent.h
                    src/pvrclient-nextpvr.h
                    src/Socket.h
                    src/uri.h
                    src/buffers/Buffer.h
                    src/buffers/DummyBuffer.h
                    src/buffers/TimeshiftBuffer.h
                    src/buffers/RecordingBuffer.h
                    src/buffers/CircularBuffer.h
                    src/buffers/Seeker.h)

SET(DEPLIBS ${p8-platform_LIBRARIES}
            ${TINYXML_LIBRARIES})
if(WIN32)
  list(APPEND DEPLIBS ws2_32)
  add_definitions(/D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

build_addon(pvr.nextpvr NEXTPVR DEPLIBS)

include(CPack)

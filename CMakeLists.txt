project(pvr.nextpvr)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(kodi REQUIRED)
find_package(kodiplatform REQUIRED)
find_package(TinyXML REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${TINYXML_INCLUDE_DIR}
                    ${KODI_INCLUDE_DIR})

set(NEXTPVR_SOURCES src/client.cpp
                    src/DialogRecordPref.cpp
                    src/liveshift.cpp
                    src/md5.cpp
                    src/pvrclient-nextpvr.cpp
                    src/RingBuffer.cpp
                    src/Socket.cpp
                    src/uri.cpp)

SET(DEPLIBS ${kodiplatform_LIBRARIES}
            ${TINYXML_LIBRARIES})
if(WIN32)
  list(APPEND DEPLIBS ws2_32)
endif()

build_addon(pvr.nextpvr NEXTPVR DEPLIBS)

include(CPack)

/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "Settings.h"
#if defined(TARGET_WINDOWS)
  #define WIN32_LEAN_AND_MEAN
  #include "windows.h"
#endif
#include <kodi/Filesystem.h>
#include <ctime>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>

#define HTTP_OK 200
#define HTTP_NOTFOUND 404
#define HTTP_BADREQUEST 400



namespace NextPVR
{
  class Request
  {
  public:
    /*
      * Singleton getter for the instance
      */
    static Request& GetInstance()
    {
      static Request request;
      return request;
    }
    int DoRequest(const char* resource, std::string& response);
    int FileCopy(const char* resource, std::string fileName);
    void setSID(char* newsid) { strcpy(m_sid, newsid); };
    bool PingBackend();
    bool OneTimeSetup();
    const char* getSID() { return m_sid; };
    std::vector<std::vector<std::string>> Discovery();

  private:
    Request() = default;

    Request(Request const&) = delete;
    void operator=(Request const&) = delete;

    Settings& m_settings = Settings::GetInstance();
    mutable std::mutex m_mutexRequest;
    time_t m_start = 0;
    char m_sid[64]{0};
  };
} // namespace NextPVR

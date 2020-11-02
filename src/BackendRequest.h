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
#include "tinyxml2.h"

#define HTTP_OK 200
#define HTTP_NOTFOUND 404
#define HTTP_BADREQUEST 400



namespace NextPVR
{
  class ATTRIBUTE_HIDDEN Request
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
    int DoRequest(std::string resource, std::string& response);
    bool DoActionRequest(std::string resource);
    tinyxml2::XMLError DoMethodRequest(std::string resource, tinyxml2::XMLDocument& doc, bool compresssed = true);
    int FileCopy(const char* resource, std::string fileName);
    tinyxml2::XMLError  GetLastUpdate(std::string resource, time_t& last_update);
    bool PingBackend();
    bool OneTimeSetup();
    const char* GetSID() { return m_sid.c_str(); };
    std::vector<std::vector<std::string>> Discovery();

    void SetSID(std::string newsid) { m_sid = newsid; };
    void ClearSID() { m_sid.clear(); m_sidUpdate = 0; };
    bool IsActiveSID() { return !m_sid.empty() && time(nullptr) < m_sidUpdate + 900; };

  private:
    Request() = default;

    Request(Request const&) = delete;
    void operator=(Request const&) = delete;

    Settings& m_settings = Settings::GetInstance();
    mutable std::mutex m_mutexRequest;
    time_t m_start = 0;
    std::string m_sid;
    time_t m_sidUpdate = 0;
  };
} // namespace NextPVR

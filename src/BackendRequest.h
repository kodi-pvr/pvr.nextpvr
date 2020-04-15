/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <ctime>
#include <stdio.h>
#include <stdlib.h>

#include "client.h"
#include "p8-platform/threads/mutex.h"

#define HTTP_OK 200
#define HTTP_NOTFOUND 404
#define HTTP_BADREQUEST 400

using namespace ADDON;

namespace NextPVR
{
  class Request
  {
    public:
      int DoRequest(const char *resource, std::string &response);
      int FileCopy(const char *resource, std::string fileName);
      void setSID(char *newsid) {strcpy(m_sid,newsid);};
      bool PingBackend();
      const char *getSID() {return m_sid;};
      std::vector<std::string> Discovery();
      Request(void){};
      virtual ~Request() {};
    private:
      P8PLATFORM::CMutex        m_mutexRequest;
      time_t m_start;
      char m_sid[64];
  };
  extern Request *m_backEnd;
}
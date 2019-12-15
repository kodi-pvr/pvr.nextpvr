#pragma once
/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

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
      Request(void){};
      virtual ~Request() {};
    private:
      P8PLATFORM::CMutex        m_mutexRequest;
      time_t m_start;
      char m_sid[64];
  };
  extern Request *m_backEnd;
}
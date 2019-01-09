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


#include  "BackendRequest.h"


#define HTTP_OK 200
#define HTTP_NOTFOUND 404
#define HTTP_BADREQUEST 400

using namespace ADDON;
using namespace NextPVR;

namespace NextPVR
{
  int Request::DoRequest(const char *resource, std::string &response, char *m_sid)
  {
    P8PLATFORM::CLockObject lock(m_mutexRequest);

// build request string, adding SID if requred
    char strURL[1024];

    if (strstr(resource, "method=session") == NULL)
      snprintf(strURL,sizeof(strURL),"http://%s:%d%s&sid=%s", g_szHostname.c_str(), g_iPort, resource, m_sid);
    else
      snprintf(strURL,sizeof(strURL),"http://%s:%d%s", g_szHostname.c_str(), g_iPort, resource);

    // ask XBMC to read the URL for us
    int resultCode = HTTP_NOTFOUND;
    void* fileHandle = XBMC->OpenFile(strURL, 0);
    if (fileHandle)
    {
      char buffer[1024];
      while (XBMC->ReadFileString(fileHandle, buffer, 1024))
      {
        response.append(buffer);
      }
      XBMC->CloseFile(fileHandle);
      resultCode = HTTP_OK;
      if (response.empty() || strstr(response.c_str(), "<rsp stat=\"ok\">") == NULL)
      {
          XBMC->Log(LOG_ERROR, "DoRequest failed, response=%s", response.c_str());
          resultCode = HTTP_BADREQUEST;
      }
    }
    return resultCode;
  }
  Request::Request()
  {
    XBMC->Log(LOG_ERROR, "%s %d", __FUNCTION__, __LINE__);
  }
  Request::~Request()
  {
    XBMC->Log(LOG_ERROR, "%s %d", __FUNCTION__, __LINE__);
  }
}
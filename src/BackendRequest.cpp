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

using namespace ADDON;

namespace NextPVR
{
  Request *m_backEnd;
  int Request::DoRequest(const char *resource, std::string &response)
  {
    P8PLATFORM::CLockObject lock(m_mutexRequest);
    m_start = time(nullptr);
    // build request string, adding SID if requred
    char strURL[1024];

    if (strstr(resource, "method=session") == NULL)
      snprintf(strURL,sizeof(strURL),"http://%s:%d%s&sid=%s", g_szHostname.c_str(), g_iPort, resource, m_sid);
    else
      snprintf(strURL,sizeof(strURL),"http://%s:%d%s", g_szHostname.c_str(), g_iPort, resource);

    // ask XBMC to read the URL for us
    int resultCode = HTTP_NOTFOUND;
    void* fileHandle = XBMC->OpenFile(strURL, XFILE::READ_NO_CACHE);
    if (fileHandle)
    {
      char buffer[1024];
      while (XBMC->ReadFileString(fileHandle, buffer, 1024))
      {
        response.append(buffer);
      }
      XBMC->CloseFile(fileHandle);
      resultCode = HTTP_OK;
      if ((response.empty() || strstr(response.c_str(), "<rsp stat=\"ok\">") == NULL) && strstr(resource, "method=channel.stream.info") == NULL )
      {
        XBMC->Log(LOG_ERROR, "DoRequest failed, response=%s", response.c_str());
        resultCode = HTTP_BADREQUEST;
      }
    }
    XBMC->Log(LOG_DEBUG, "DoRequest return %s %d %d %d", resource, resultCode,response.length(),time(nullptr) -m_start);

    return resultCode;
  }
  int Request::FileCopy(const char *resource,std::string fileName)
  {
    P8PLATFORM::CLockObject lock(m_mutexRequest);
    int written = 0;
    m_start = time(nullptr);

    char strURL[1024];
    char separator = (strchr(resource,'?') == nullptr) ?  '?' : '&';
    snprintf(strURL,sizeof(strURL),"http://%s:%d%s%csid=%s", g_szHostname.c_str(), g_iPort, resource, separator, m_sid);

    // ask XBMC to read the URL for us
    int resultCode = HTTP_NOTFOUND;
    void* inputFile = XBMC->OpenFile(strURL, XFILE::READ_NO_CACHE);
    int datalen;
    if (inputFile)
    {
      void* outputFile = XBMC->OpenFileForWrite(fileName.c_str(), true);
      if (outputFile)
      {
        char buffer[1024];
        while ((datalen=XBMC->ReadFile(inputFile, buffer, sizeof(buffer))))
        {
          XBMC->WriteFile(outputFile, buffer, datalen);
          written += datalen;
        }
        XBMC->CloseFile(inputFile);
        XBMC->CloseFile(outputFile);
        resultCode = HTTP_OK;
      }
    }
    if (written == 0)
    {
      resultCode = HTTP_BADREQUEST;
    }
    XBMC->Log(LOG_DEBUG, "FileCopy (%s - %s) %d %d %d", resource, fileName.c_str(), resultCode,written,time(nullptr) -m_start);

    return resultCode;
  }
  bool Request::PingBackend()
  {
    char strURL[1024];
    snprintf(strURL,sizeof(strURL),"http://%s:%d%s|connection-timeout=2", g_szHostname.c_str(), g_iPort, "/service?method=recording.lastupdated");
    void* fileHandle = XBMC->OpenFile(strURL, XFILE::READ_NO_CACHE);
    if (fileHandle)
    {
      XBMC->CloseFile(fileHandle);
      return true;
    }
    return false;
  }
}
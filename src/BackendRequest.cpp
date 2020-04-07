/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include  "BackendRequest.h"
#include "Socket.h"
#include <p8-platform/util/StringUtils.h>

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
    void* fileHandle = XBMC->OpenFile(strURL, READ_NO_CACHE);
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
    void* inputFile = XBMC->OpenFile(strURL, READ_NO_CACHE);
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
    void* fileHandle = XBMC->OpenFile(strURL, READ_NO_CACHE);
    if (fileHandle)
    {
      XBMC->CloseFile(fileHandle);
      return true;
    }
    return false;
  }
  std::vector<std::string> Request::Discovery()
  {
    std::vector<std::string> foundAddress;
    Socket* socket = new Socket(af_inet, pf_inet, sock_dgram, udp);
    if (socket->create())
    {
      bool optResult;
      int broadcast = 1;
      if (optResult = socket->SetSocketOption(SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&broadcast), sizeof(broadcast)))
        XBMC->Log(LOG_ERROR, "SO_REUSEADDR %d", optResult);
      broadcast = 1;
      if (optResult = socket->SetSocketOption(SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcast), sizeof(broadcast)))
        XBMC->Log(LOG_ERROR, "SO_BROADCAST %d", optResult);
#if defined(TARGET_WINDOWS)
      DWORD timeout = 5 * 1000;
      if (optResult = socket->SetSocketOption(SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout)))
        XBMC->Log(LOG_ERROR, "WINDOWS SO_RCVTIMEO %d", optResult);
#else
      struct timeval tv;
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      if (optResult = socket->SetSocketOption( SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv)))
        XBMC->Log(LOG_ERROR, "SO_RCVTIMEO %d", optResult);
#endif

      const char msg[] = "Kodi pvr.nextpvr broadcast";
      if (socket->BroadcastSendTo(16891, msg, sizeof(msg)) > 0)
      {
        int sockResult;
        do {
          char response[512]{ 0 };
          if (sockResult = socket->BroadcastReceiveFrom(response, 512) > 0)
          {
            std::vector<std::string> parseResponse = StringUtils::Split(response, ":");
            if (parseResponse.size() >= 3)
            {
              XBMC->Log(LOG_INFO, "Broadcast received %s %s", parseResponse[0].c_str(), parseResponse[1].c_str());
              if (foundAddress.empty())
              {
                foundAddress = parseResponse;
              }
            }
          }
        } while (sockResult > 0);
      }
    }
    socket->close();
    return foundAddress;
  }
}
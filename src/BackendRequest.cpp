/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include  "BackendRequest.h"
#include "Socket.h"
#include <libKODI_guilib.h>
#include <p8-platform/util/StringUtils.h>

using namespace ADDON;

namespace NextPVR
{
  int Request::DoRequest(const char *resource, std::string &response)
  {
    P8PLATFORM::CLockObject lock(m_mutexRequest);
    m_start = time(nullptr);
    // build request string, adding SID if requred
    char strURL[1024];

    if (strstr(resource, "method=session") == NULL)
      snprintf(strURL,sizeof(strURL),"http://%s:%d%s&sid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, resource, m_sid);
    else
      snprintf(strURL,sizeof(strURL),"http://%s:%d%s", m_settings.m_hostname.c_str(), m_settings.m_port, resource);

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
    snprintf(strURL,sizeof(strURL),"http://%s:%d%s%csid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, resource, separator, m_sid);

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
    snprintf(strURL,sizeof(strURL),"http://%s:%d%s|connection-timeout=2", m_settings.m_hostname.c_str(), m_settings.m_port, "/service?method=recording.lastupdated");
    void* fileHandle = XBMC->OpenFile(strURL, READ_NO_CACHE);
    if (fileHandle)
    {
      XBMC->CloseFile(fileHandle);
      return true;
    }
    return false;
  }
  bool Request::OneTimeSetup(void *hdl)
  {
    // create user folder for channel icons and try and locate backend
    #if defined(TARGET_WINDOWS)
      #undef CreateDirectory
    #endif
    const std::string backend = "http://127.0.0.1:8866/service?method=recording.lastupdated|connection-timeout=2";
    void* fileHandle = XBMC->OpenFile(backend.c_str(), READ_NO_CACHE);
    if (fileHandle)
    {
      XBMC->CloseFile(fileHandle);
      XBMC->CreateDirectory("special://userdata/addon_data/pvr.nextpvr/");
      return true;
    }
    else
    {
      // couldn't find NextPVR on localhost so try and find on subnet
      std::vector<std::vector<std::string>> foundAddress = Discovery();
      int offset = 0;
      if (!foundAddress.empty())
      {
        // found host using discovery protocol.
        if (foundAddress.size() > 1)
        {
          // found multiple hosts let user choose
          CHelper_libKODI_guilib* GUI = new CHelper_libKODI_guilib;
          GUI->RegisterMe(hdl);
          const char* entries[5];
          int entry;
          for (entry = 0; entry < 5 && entry < foundAddress.size(); entry++)
          {
            entries[entry] = foundAddress[entry][0].c_str();
          }
          offset = GUI->Dialog_Select(XBMC->GetLocalizedString(30187), entries, entry, 0);
          delete GUI;
          if (offset < 0)
          {
            XBMC->Log(LOG_INFO,"User canceled setup expect a failed install");
            return false;
          }
        }
        XBMC->CreateDirectory("special://userdata/addon_data/pvr.nextpvr/");
        m_settings.UpdateServerPort(foundAddress[offset][0], std::stoi(foundAddress[offset][1]));
        XBMC->QueueNotification(QUEUE_INFO, XBMC->GetLocalizedString(30182), m_settings.m_hostname.c_str(), m_settings.m_port);
        char *settings = XBMC->TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/settings.xml");
        // create settings.xml with host and port
        void* settingsFile = XBMC->OpenFileForWrite(settings,false);
        const char tmp[] = "<settings version=\"2\">\n</settings>\n";
        XBMC->WriteFile(settingsFile,tmp, strlen(tmp));
        XBMC->CloseFile(settingsFile);
        m_settings.SaveSettings("host",m_settings.m_hostname);
        m_settings.SaveSettings("port",std::to_string(m_settings.m_port));
        XBMC->FreeString(settings);
        return true;
      }
    }
    return false;
  }
  std::vector<std::vector<std::string>> Request::Discovery()
  {
    std::vector<std::vector<std::string>> foundAddress;
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
              foundAddress.push_back(parseResponse);
            }
          }
        } while (sockResult > 0);
      }
    }
    socket->close();
    return foundAddress;
  }
}

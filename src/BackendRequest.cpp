/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "BackendRequest.h"
#include "Socket.h"
#include <kodi/General.h>
#include <kodi/gui/dialogs/Select.h>
#include <p8-platform/util/StringUtils.h>

namespace NextPVR
{
  int Request::DoRequest(const char* resource, std::string& response)
  {
    std::unique_lock<std::mutex> lock(m_mutexRequest);
    m_start = time(nullptr);
    // build request string, adding SID if requred
    std::string URL;

    if (strstr(resource, "method=session") == nullptr)
      URL = StringUtils::Format("http://%s:%d%s&sid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, resource, m_sid);
    else
      URL = StringUtils::Format("http://%s:%d%s", m_settings.m_hostname.c_str(), m_settings.m_port, resource);

    // ask XBMC to read the URL for us
    int resultCode = HTTP_NOTFOUND;
    kodi::vfs::CFile stream;
    if (stream.OpenFile(URL, ADDON_READ_NO_CACHE))
    {
      char buffer[1025] = {0};
      int count;
      while ((count=stream.Read(buffer, 1024)))
      {
        response.append(buffer, count);
      }
      stream.Close();
      resultCode = HTTP_OK;
      if ((response.empty() || strstr(response.c_str(), "<rsp stat=\"ok\">") == nullptr) && strstr(resource, "method=channel.stream.info") == nullptr)
      {
        kodi::Log(ADDON_LOG_ERROR, "DoRequest failed, response=%s", response.c_str());
        resultCode = HTTP_BADREQUEST;
      }
    }
    kodi::Log(ADDON_LOG_DEBUG, "DoRequest return %s %d %d %d", resource, resultCode, response.length(), time(nullptr) - m_start);

    return resultCode;
  }
  int Request::FileCopy(const char* resource, std::string fileName)
  {
    std::unique_lock<std::mutex> lock(m_mutexRequest);
    ssize_t written = 0;
    m_start = time(nullptr);


    char separator = (strchr(resource, '?') == nullptr) ? '?' : '&';
    const std::string URL = StringUtils::Format("http://%s:%d%s%csid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, resource, separator, m_sid);

    // ask XBMC to read the URL for us
    int resultCode = HTTP_NOTFOUND;
    kodi::vfs::CFile inputStream;
    ssize_t datalen;
    if (inputStream.OpenFile(URL, ADDON_READ_NO_CACHE))
    {
      kodi::vfs::CFile outputFile;
      if (outputFile.OpenFileForWrite(fileName, ADDON_READ_NO_CACHE))
      {
        char buffer[1024];
        while ((datalen = inputStream.Read( buffer, sizeof(buffer))))
        {
          outputFile.Write( buffer, datalen);
          written += datalen;
        }
        inputStream.Close();
        outputFile.Close();
        resultCode = HTTP_OK;
      }
    }
    if (written == 0)
    {
      resultCode = HTTP_BADREQUEST;
    }
    kodi::Log(ADDON_LOG_DEBUG, "FileCopy (%s - %s) %zu %d %d", resource, fileName.c_str(), resultCode, written, time(nullptr) - m_start);

    return resultCode;
  }
  bool Request::PingBackend()
  {
    const std::string URL = StringUtils::Format("http://%s:%d%s|connection-timeout=2", m_settings.m_hostname.c_str(), m_settings.m_port, "/service?method=recording.lastupdated");
    kodi::vfs::CFile backend;
    if (backend.OpenFile(URL, ADDON_READ_NO_CACHE))
    {
      backend.Close();
      return true;
    }
    return false;
  }
  bool Request::OneTimeSetup()
  {
    // create user folder for channel icons and try and locate backend
    const std::string URL = "http://127.0.0.1:8866/service?method=recording.lastupdated|connection-timeout=2";
    kodi::vfs::CFile backend;
    if (backend.OpenFile(URL, ADDON_READ_NO_CACHE))
    {
      backend.Close();
      kodi::vfs::CreateDirectory("special://userdata/addon_data/pvr.nextpvr/");
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
          std::vector<std::string> entries;
          for (int entry = 0; entry < foundAddress.size(); entry++)
          {
            entries.emplace_back(foundAddress[entry][0]);
          }
          offset = kodi::gui::dialogs::Select::Show(kodi::GetLocalizedString(30187), entries);
          if (offset < 0)
          {
            kodi::Log(ADDON_LOG_INFO, "User canceled setup expect a failed install");
            return false;
          }
        }
        kodi::vfs::CreateDirectory("special://userdata/addon_data/pvr.nextpvr/");
        m_settings.UpdateServerPort(foundAddress[offset][0], std::stoi(foundAddress[offset][1]));
        kodi::QueueNotification(QUEUE_INFO, kodi::GetLocalizedString(30189),
                StringUtils::Format(kodi::GetLocalizedString(30182).c_str(), m_settings.m_hostname.c_str(), m_settings.m_port));
        /* note that these run before the file is created */
        kodi::SetSettingString("host", m_settings.m_hostname);
        kodi::SetSettingInt("port", m_settings.m_port);
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
        kodi::Log(ADDON_LOG_ERROR, "SO_REUSEADDR %d", optResult);
      broadcast = 1;
      if (optResult = socket->SetSocketOption(SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcast), sizeof(broadcast)))
        kodi::Log(ADDON_LOG_ERROR, "SO_BROADCAST %d", optResult);
#if defined(TARGET_WINDOWS)
      DWORD timeout = 5 * 1000;
      if (optResult = socket->SetSocketOption(SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout)))
        kodi::Log(ADDON_LOG_ERROR, "WINDOWS SO_RCVTIMEO %d", optResult);
#else
      struct timeval tv;
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      if (optResult = socket->SetSocketOption(SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv)))
        kodi::Log(ADDON_LOG_ERROR, "SO_RCVTIMEO %d", optResult);
#endif

      const char msg[] = "Kodi pvr.nextpvr broadcast";
      if (socket->BroadcastSendTo(16891, msg, sizeof(msg)) > 0)
      {
        int sockResult;
        do
        {
          char response[512]{0};
          if (sockResult = socket->BroadcastReceiveFrom(response, 512) > 0)
          {
            std::vector<std::string> parseResponse = StringUtils::Split(response, ":");
            if (parseResponse.size() >= 3)
            {
              kodi::Log(ADDON_LOG_INFO, "Broadcast received %s %s", parseResponse[0].c_str(), parseResponse[1].c_str());
              foundAddress.push_back(parseResponse);
            }
          }
        } while (sockResult > 0);
      }
    }
    socket->close();
    return foundAddress;
  }
} // namespace NextPVR

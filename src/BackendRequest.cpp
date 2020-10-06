/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "BackendRequest.h"
#include "Socket.h"
#include "utilities/XMLUtils.h"
#include <kodi/General.h>
#include <kodi/Network.h>
#include <kodi/gui/dialogs/Select.h>
#include <kodi/tools/StringUtils.h>

using namespace NextPVR::utilities;

namespace NextPVR
{
  int Request::DoRequest(const char* resource, std::string& response)
  {
    auto start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(m_mutexRequest);
    // build request string, adding SID if requred
    std::string URL;

    if (strstr(resource, "method=session") == nullptr)
      URL = kodi::tools::StringUtils::Format("%s%s&sid=%s", m_settings.m_urlBase, resource, GetSID());
    else
      URL = kodi::tools::StringUtils::Format("%s%s", m_settings.m_urlBase, resource);

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
      else
      {
        m_sidUpdate = time(nullptr);
      }
    }
    kodi::Log(ADDON_LOG_DEBUG, "DoRequest return %s %d %d %d", resource, resultCode, response.length(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start));
    return resultCode;
  }

  tinyxml2::XMLError Request::DoMethodRequest(const char* resource, tinyxml2::XMLDocument& doc, bool compressed)
  {
    auto start = std::chrono::steady_clock::now();
    // return is same on timeout or http return ie 404, 500.
    tinyxml2::XMLError retError = tinyxml2::XML_ERROR_FILE_NOT_FOUND;
    std::unique_lock<std::mutex> lock(m_mutexRequest);
    m_start = time(nullptr);
    // build request string, adding SID if requred
    std::string URL;

    if (IsActiveSID())
      URL = kodi::tools::StringUtils::Format("%s%s&sid=%s", m_settings.m_urlBase, resource, GetSID());
    else
      URL = kodi::tools::StringUtils::Format("%s%s", m_settings.m_urlBase, resource);

    if (!compressed)
      URL += "|Accept-Encoding=identity";

    // ask XBMC to read the URL for us
    kodi::vfs::CFile stream;
    std::string response;
    if (stream.OpenFile(URL, ADDON_READ_NO_CACHE))
    {
      char buffer[1025]{ 0 };
      int count;
      while ((count = stream.Read(buffer, 1024)))
      {
        response.append(buffer, count);
      }
      stream.Close();
      retError = doc.Parse(response.c_str());
      if (retError == tinyxml2::XML_SUCCESS)
      {
        const char* attrib = doc.RootElement()->Attribute("stat");
        if ( attrib == nullptr || strcmp(attrib, "ok"))
        {
          kodi::Log(ADDON_LOG_DEBUG, "DoMethodRequest bad return %s", attrib);
          retError = tinyxml2::XML_NO_ATTRIBUTE;
        }
        else
        {
          m_sidUpdate = time(nullptr);
        }
      }
    }
    kodi::Log(ADDON_LOG_DEBUG, "DoMethodRequest return %s %d %d %d", resource, retError, response.length(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start));
    return retError;
  }

  tinyxml2::XMLError Request::GetLastUpdate(const char* resource, time_t& last_update)
  {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError xmlReturn = DoMethodRequest(resource, doc, false);
    if ( xmlReturn == tinyxml2::XML_SUCCESS)
    {
      int64_t value{ 0 };
      if (!XMLUtils::GetLong(doc.RootElement(), "last_update", value))
      {
        xmlReturn = tinyxml2::XML_NO_TEXT_NODE;
      }
      last_update = value + m_settings.m_serverTimeOffset;
    }
    return xmlReturn;
  }

  int Request::FileCopy(const char* resource, std::string fileName)
  {
    std::unique_lock<std::mutex> lock(m_mutexRequest);
    ssize_t written = 0;
    m_start = time(nullptr);


    char separator = (strchr(resource, '?') == nullptr) ? '?' : '&';
    const std::string URL = kodi::tools::StringUtils::Format("%s%s%csid=%s", m_settings.m_urlBase, resource, separator, GetSID());

    // ask XBMC to read the URL for us
    int resultCode = HTTP_NOTFOUND;
    kodi::vfs::CFile inputStream;
    ssize_t datalen;
    if (inputStream.OpenFile(URL, ADDON_READ_NO_CACHE))
    {
      kodi::vfs::CFile outputFile;
      if (outputFile.OpenFileForWrite(fileName))
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
    const std::string URL = kodi::tools::StringUtils::Format("%s%s|connection-timeout=2", m_settings.m_urlBase, "/service?method=recording.lastupdated");
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
    std::vector<std::vector<std::string>> foundAddress = Discovery();
    if (!foundAddress.empty())
    {
      // found host using discovery protocol.
      std::vector<std::string> entries;
      int offset = 0;
      for (unsigned int entry = 0; entry < foundAddress.size(); entry++)
      {
        if (kodi::network::IsLocalHost(foundAddress[entry][0]))
          entries.emplace_back("127.0.0.1");
        else
          entries.emplace_back(foundAddress[entry][0]);
      }
      // found multiple hosts using discovery protocol.
      if (foundAddress.size() > 1)
      {
        offset = kodi::gui::dialogs::Select::Show(kodi::GetLocalizedString(30187), entries);
      }
      if (offset >= 0)
      {
        kodi::vfs::CreateDirectory("special://userdata/addon_data/pvr.nextpvr/");
        m_settings.UpdateServerPort(entries[offset], atoi(foundAddress[offset][1].c_str()));
        kodi::QueueNotification(QUEUE_INFO, kodi::GetLocalizedString(30189),
          kodi::tools::StringUtils::Format(kodi::GetLocalizedString(30182).c_str(), m_settings.m_hostname.c_str(), m_settings.m_port));
        /* note that these run before the file is created */
        kodi::SetSettingString("host", m_settings.m_hostname);
        kodi::SetSettingInt("port", m_settings.m_port);
        return true;
      }
    }
    // try 127.0.0.1
    const std::string URL = "http://127.0.0.1:8866/service?method=recording.lastupdated|connection-timeout=2";
    kodi::vfs::CFile backend;
    if (backend.OpenFile(URL, ADDON_READ_NO_CACHE))
    {
      backend.Close();
      kodi::vfs::CreateDirectory("special://userdata/addon_data/pvr.nextpvr/");
      return true;
    }
    kodi::Log(ADDON_LOG_INFO, "No running server found expect a failed install");
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
      if ((optResult = socket->SetSocketOption(SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&broadcast), sizeof(broadcast))))
        kodi::Log(ADDON_LOG_ERROR, "SO_REUSEADDR %d", optResult);
      broadcast = 1;
      if ((optResult = socket->SetSocketOption(SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcast), sizeof(broadcast))))
        kodi::Log(ADDON_LOG_ERROR, "SO_BROADCAST %d", optResult);
#if defined(TARGET_WINDOWS)
      DWORD timeout = 5 * 1000;
      if (optResult = socket->SetSocketOption(SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout)))
        kodi::Log(ADDON_LOG_ERROR, "WINDOWS SO_RCVTIMEO %d", optResult);
#else
      struct timeval tv;
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      if ((optResult = socket->SetSocketOption(SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv))))
        kodi::Log(ADDON_LOG_ERROR, "SO_RCVTIMEO %d", optResult);
#endif

      const char msg[] = "Kodi pvr.nextpvr broadcast";
      if (socket->BroadcastSendTo(16891, msg, sizeof(msg)) > 0)
      {
        int sockResult;
        do
        {
          char response[512]{0};
          if ((sockResult = socket->BroadcastReceiveFrom(response, 512) > 0))
          {
            std::vector<std::string> parseResponse = kodi::tools::StringUtils::Split(response, ":");
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

/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#include "Settings.h"
#include "BackendRequest.h"
#include "uri.h"
#include "utilities/XMLUtils.h"

#include <kodi/General.h>
#include <p8-platform/util/StringUtils.h>
#include <tinyxml.h>

using namespace NextPVR;
using namespace NextPVR::utilities;

/***************************************************************************
 * PVR settings
 **************************************************************************/
void Settings::ReadFromAddon()
{
  std::string buffer;

  /* Connection settings */
  /***********************/
  m_hostname = kodi::GetSettingString("host", DEFAULT_HOST);
  uri::decode(m_hostname);

  m_port = kodi::GetSettingInt("port", DEFAULT_PORT);

  m_PIN = kodi::GetSettingString("pin", DEFAULT_PIN);

  m_enableWOL = kodi::GetSettingBoolean("wolenable", false);
  m_hostMACAddress = kodi::GetSettingString("host_mac");
  if (m_enableWOL)
  {
    if (m_hostMACAddress.empty())
      m_enableWOL = false;
    else if (m_hostname == "127.0.0.1" || m_hostname == "localhost" || m_hostname == "::1")
      m_enableWOL = false;
  }

  m_timeoutWOL = kodi::GetSettingInt("woltimeout", 20);

  m_downloadGuideArtwork = kodi::GetSettingBoolean("guideartwork" ,DEFAULT_GUIDE_ARTWORK);

  m_remoteAccess = kodi::GetSettingBoolean("remoteaccess", false);

  m_flattenRecording = kodi::GetSettingBoolean("flattenrecording", false);

  m_kodiLook = kodi::GetSettingBoolean("kodilook", false);

  m_prebuffer = kodi::GetSettingInt("prebuffer", 8);

  m_prebuffer5 = kodi::GetSettingInt("prebuffer5", 0);

  m_liveChunkSize = kodi::GetSettingInt("chunklivetv", 64);

  m_chunkRecording = kodi::GetSettingInt("chunkrecording", 32);

  m_resolution = kodi::GetSettingString("resolution",  "720");

  m_showRadio = kodi::GetSettingBoolean("showradio", true);

  /* Log the current settings for debugging purposes */
  kodi::Log(ADDON_LOG_DEBUG, "settings: host='%s', port=%i, mac=%4.4s...", m_hostname.c_str(), m_port, m_hostMACAddress.c_str());

}

ADDON_STATUS Settings::ReadBackendSettings()
{
  // check server version
  std::string settings;
  Request& request = Request::GetInstance();
  if (request.DoRequest("/service?method=setting.list", settings) == HTTP_OK)
  {
    TiXmlDocument settingsDoc;
    if (settingsDoc.Parse(settings.c_str()) != nullptr)
    {
      //dump_to_log(&settingsDoc, 0);
      if (XMLUtils::GetInt(settingsDoc.RootElement(), "NextPVRVersion", m_backendVersion))
      {
        // NextPVR server
        kodi::Log(ADDON_LOG_INFO, "NextPVR version: %d", m_backendVersion);

        // is the server new enough
        if (m_backendVersion < 40204)
        {
          kodi::Log(ADDON_LOG_ERROR, "NextPVR version '%d' is too old. Please upgrade to '%s' or higher!", m_backendVersion, NEXTPVRC_MIN_VERSION_STRING);
          kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30050), StringUtils::Format(kodi::GetLocalizedString(30051).c_str(), NEXTPVRC_MIN_VERSION_STRING));
          return ADDON_STATUS_PERMANENT_FAILURE;
        }
      }

      // load padding defaults
      m_defaultPrePadding = 1;
      XMLUtils::GetInt(settingsDoc.RootElement(), "PrePadding", m_defaultPrePadding);

      m_defaultPostPadding = 2;
      XMLUtils::GetInt(settingsDoc.RootElement(), "PostPadding", m_defaultPostPadding);

      m_showNew = false;
      XMLUtils::GetBoolean(settingsDoc.RootElement(), "ShowNewInGuide", m_showNew);

      std::string recordingDirectories;
      if (XMLUtils::GetString(settingsDoc.RootElement(), "RecordingDirectories", recordingDirectories))
      {
        m_recordingDirectories = StringUtils::Split(recordingDirectories, ",", 0);
      }

      int serverTimestamp;
      if (XMLUtils::GetInt(settingsDoc.RootElement(), "TimeEpoch", serverTimestamp))
      {
        m_serverTimeOffset = time(nullptr) - serverTimestamp;
        kodi::Log(ADDON_LOG_INFO, "Server time offset in seconds: %d", m_serverTimeOffset);
      }

      if (XMLUtils::GetInt(settingsDoc.RootElement(), "SlipSeconds", m_timeshiftBufferSeconds))
        kodi::Log(ADDON_LOG_INFO, "time shift buffer in seconds: %d", m_timeshiftBufferSeconds);

      std::string serverMac;
      if (XMLUtils::GetString(settingsDoc.RootElement(), "ServerMAC", serverMac))
      {
        std::string macAddress = serverMac.substr(0, 2) ;
        for (int i = 2; i < 12; i+=2)
        {
          macAddress+= ":" + serverMac.substr(i, 2);
        }
        kodi::Log(ADDON_LOG_DEBUG, "Server MAC address %4.4s...", macAddress.c_str());
        if (m_hostMACAddress != macAddress)
        {
          kodi::SetSettingString("host_mac", macAddress);
        }
      }
    }
  }
  return ADDON_STATUS_OK;
}

void Settings::SetVersionSpecificSettings()
{
  m_liveStreamingMethod = DEFAULT_LIVE_STREAM;

  if ((m_backendVersion < 50000) != kodi::GetSettingBoolean("legacy", false))
  {
    kodi::SetSettingEnum<eStreamingMethod>("livestreamingmethod5", eStreamingMethod::Default);
    kodi::SetSettingBoolean("legacy", m_backendVersion < 50000);
  }

  eStreamingMethod streamingMethod;
  if (kodi::CheckSettingEnum<eStreamingMethod>("livestreamingmethod", streamingMethod))
  {
    m_liveStreamingMethod = streamingMethod;
    // has v4 setting
    if (m_backendVersion < 50000)
    {
      // previous Matrix clients had a transcoding option
      if (m_liveStreamingMethod == eStreamingMethod::Transcoded)
      {
        m_liveStreamingMethod = eStreamingMethod::RealTime;
        kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30050), StringUtils::Format(kodi::GetLocalizedString(30051).c_str(), "5"));
      }
    }
    else if (m_backendVersion < 50002)
    {
      m_liveStreamingMethod = eStreamingMethod::RealTime;
      kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30050), StringUtils::Format(kodi::GetLocalizedString(30051).c_str(), "5.0.3"));
    }
    else
    {
      // check for new v5 setting with no settings.xml
      eStreamingMethod oldMethod = m_liveStreamingMethod;

      if (kodi::CheckSettingEnum<eStreamingMethod>("livestreamingmethod5", streamingMethod))
        m_liveStreamingMethod = streamingMethod;

      if (m_liveStreamingMethod == eStreamingMethod::Default)
        m_liveStreamingMethod = oldMethod;

      if (m_liveStreamingMethod == RollingFile || m_liveStreamingMethod == Timeshift)
        m_liveStreamingMethod = eStreamingMethod::ClientTimeshift;

    }
  }

  if (m_backendVersion >= 50000)
  {
    m_sendSidWithMetadata = false;
    if (m_PIN != "0000" && m_remoteAccess)
    {
      m_downloadGuideArtwork = false;
      m_sendSidWithMetadata = true;
    }

    m_guideArtPortrait = kodi::GetSettingBoolean("guideartworkportrait", false);

    m_showRecordingSize = kodi::GetSettingBoolean("recordingsize", false);

    m_transcodedTimeshift = kodi::GetSettingBoolean("ffmpegdirect", false);
  }
  else
  {
    m_sendSidWithMetadata = true;
    m_showRecordingSize = false;
  }
}

bool Settings::SaveSettings(std::string name, std::string value)
{
  bool found = false;
  TiXmlDocument doc;

  std::string settings = kodi::vfs::TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/settings.xml");
  if (doc.LoadFile(settings))
  {
    //Get Root Node
    TiXmlElement* rootNode = doc.FirstChildElement("settings");
    if (rootNode)
    {
      TiXmlElement* childNode;
      std::string key_value;
      for (childNode = rootNode->FirstChildElement("setting"); childNode; childNode = childNode->NextSiblingElement())
      {
        if (childNode->QueryStringAttribute("id", &key_value) == TIXML_SUCCESS)
        {
          if (key_value == name)
          {
            if (childNode->FirstChild() != nullptr)
            {
              childNode->FirstChild()->SetValue(value);
              found = true;
              break;
            }
            return false;
          }
        }
      }
      if (found == false)
      {
        TiXmlElement* newSetting = new TiXmlElement("setting");
        TiXmlText* newvalue = new TiXmlText(value);
        newSetting->SetAttribute("id", name);
        newSetting->LinkEndChild(newvalue);
        rootNode->LinkEndChild(newSetting);
      }
      doc.SaveFile(settings);
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Error loading settings.xml %s", settings.c_str());
  }
  return true;
}



ADDON_STATUS Settings::SetValue(const std::string& settingName, const kodi::CSettingValue& settingValue)
{
  //Connection
  if (g_pvrclient==nullptr)
  {
    // Don't want to cause a restart after the first time discovery
    return ADDON_STATUS_OK;
  }
  if (settingName == "host")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostname, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "port")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_port, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "pin")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_PIN, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "remoteaccess")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_remoteAccess, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "showradio")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRadio, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "guideartwork")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_downloadGuideArtwork, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "guideartworkportrait")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_guideArtPortrait, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "recordingsize")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRecordingSize, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "flattenrecording")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_flattenRecording, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "kodilook")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_kodiLook, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "host_mac")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostMACAddress, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "livestreamingmethod" && m_backendVersion < 50000)
    return SetEnumSetting<eStreamingMethod, ADDON_STATUS>(settingName, settingValue, m_liveStreamingMethod, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "livestreamingmethod5" && m_backendVersion >= 50000 && settingValue.GetEnum<const eStreamingMethod>() != eStreamingMethod::Default)
    return SetEnumSetting<eStreamingMethod, ADDON_STATUS>(settingName, settingValue, m_liveStreamingMethod, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "prebuffer")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_prebuffer, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "prebuffer5")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_prebuffer5, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "chucksize")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_liveChunkSize, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "chuckrecordings")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_chunkRecording, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "resolution")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_resolution, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "ffmpegdirect")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_transcodedTimeshift, ADDON_STATUS_OK, ADDON_STATUS_OK);
  return ADDON_STATUS_OK;
}

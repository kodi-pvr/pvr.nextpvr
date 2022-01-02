/*
 *  Copyright (C) 2020-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#include "Settings.h"
#include "BackendRequest.h"
#include "uri.h"
#include "utilities/XMLUtils.h"

#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>

using namespace NextPVR;
using namespace NextPVR::utilities;

const std::string connectionFlag = "special://userdata/addon_data/pvr.nextpvr/connection.flag";

/***************************************************************************
 * PVR settings
 **************************************************************************/
void Settings::ReadFromAddon()
{
  std::string buffer;

  /* Connection settings */
  /***********************/

  std::string protocol = kodi::addon::GetSettingString("hostprotocol", DEFAULT_PROTOCOL);

  m_hostname = kodi::addon::GetSettingString("host", DEFAULT_HOST);
  uri::decode(m_hostname);

  m_port = kodi::addon::GetSettingInt("port", DEFAULT_PORT);

  m_PIN = kodi::addon::GetSettingString("pin", DEFAULT_PIN);

  sprintf(m_urlBase, "%s://%.255s:%d", protocol.c_str(), m_hostname.c_str(), m_port);

  m_enableWOL = kodi::addon::GetSettingBoolean("wolenable", false);
  m_hostMACAddress = kodi::addon::GetSettingString("host_mac");
  if (m_enableWOL)
  {
    if (m_hostMACAddress.empty())
      m_enableWOL = false;
    else if (m_hostname == "127.0.0.1" || m_hostname == "localhost" || m_hostname == "::1")
      m_enableWOL = false;
  }

  m_timeoutWOL = kodi::addon::GetSettingInt("woltimeout", 20);

  m_remoteAccess = kodi::addon::GetSettingBoolean("remoteaccess", false);

  m_liveStreamingMethod = kodi::addon::GetSettingEnum<eStreamingMethod>("livestreamingmethod5", DEFAULT_LIVE_STREAM);

  m_flattenRecording = kodi::addon::GetSettingBoolean("flattenrecording", false);

  m_separateSeasons = kodi::addon::GetSettingBoolean("separateseasons", false);

  m_kodiLook = kodi::addon::GetSettingBoolean("kodilook", false);

  m_prebuffer5 = kodi::addon::GetSettingInt("prebuffer5", 0);

  m_liveChunkSize = kodi::addon::GetSettingInt("chunklivetv", 64);

  m_chunkRecording = kodi::addon::GetSettingInt("chunkrecording", 32);

  m_ignorePadding = kodi::addon::GetSettingBoolean("ignorepadding", true);

  m_resolution = kodi::addon::GetSettingString("resolution",  "720");

  m_showRadio = kodi::addon::GetSettingBoolean("showradio", true);

  m_backendResume = kodi::addon::GetSettingBoolean("backendresume", true);

  m_connectionConfirmed = kodi::vfs::FileExists(connectionFlag);

  if (m_PIN != "0000" && m_remoteAccess)
  {
    m_downloadGuideArtwork = false;
    m_sendSidWithMetadata = true;
  }  else {
    m_downloadGuideArtwork = kodi::addon::GetSettingBoolean("guideartwork" ,DEFAULT_GUIDE_ARTWORK);
    m_sendSidWithMetadata = false;
  }

  m_guideArtPortrait = kodi::addon::GetSettingBoolean("guideartworkportrait", false);

  m_genreString = kodi::addon::GetSettingBoolean("genrestring", false);

  m_showRecordingSize = kodi::addon::GetSettingBoolean("recordingsize", false);

  m_diskSpace = kodi::addon::GetSettingString("diskspace", "Default");

  m_transcodedTimeshift = kodi::addon::GetSettingBoolean("ffmpegdirect", false);

  m_castcrew = kodi::addon::GetSettingBoolean("castcrew", false);


  /* Log the current settings for debugging purposes */
  kodi::Log(ADDON_LOG_DEBUG, "settings: host='%s', port=%i, mac=%4.4s...", m_hostname.c_str(), m_port, m_hostMACAddress.c_str());

}

ADDON_STATUS Settings::ReadBackendSettings()
{
  // check server version
  std::string settings;
  Request& request = Request::GetInstance();
  tinyxml2::XMLDocument settingsDoc;
  if (request.DoMethodRequest("setting.list", settingsDoc) == tinyxml2::XML_SUCCESS)
  {
    if (XMLUtils::GetInt(settingsDoc.RootElement(), "NextPVRVersion", m_backendVersion))
    {
      // NextPVR server
      kodi::Log(ADDON_LOG_INFO, "NextPVR version: %d", m_backendVersion);

      // is the server new enough
      if (m_backendVersion < NEXTPVRC_MIN_VERSION)
      {
        kodi::Log(ADDON_LOG_ERROR, "NextPVR version '%d' is too old. Please upgrade to '%s' or higher!", m_backendVersion, NEXTPVRC_MIN_VERSION_STRING);
        kodi::QueueNotification(QUEUE_ERROR, kodi::addon::GetLocalizedString(30050), kodi::tools::StringUtils::Format(kodi::addon::GetLocalizedString(30051).c_str(), NEXTPVRC_MIN_VERSION_STRING));
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
      m_recordingDirectories = kodi::tools::StringUtils::Split(recordingDirectories, ",", 0);
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
        kodi::addon::SetSettingString("host_mac", macAddress);
      }
    }
  }
  return ADDON_STATUS_OK;
}

void Settings::SetConnection(bool status)
{
  if (status == true)
  {
      kodi::vfs::CFile outputFile;
      outputFile.OpenFileForWrite(connectionFlag);
      m_connectionConfirmed = true;
  }
  else
  {
    kodi::vfs::DeleteFile(connectionFlag);
    m_connectionConfirmed = false;
  }
}

void Settings::SetVersionSpecificSettings()
{

  // No version specific setting

}

ADDON_STATUS Settings::SetValue(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
  //Connection
  if (g_pvrclient==nullptr)
  {
    // Don't want to cause a restart after the first time discovery
    return ADDON_STATUS_OK;
  }
  if (settingName == "host")
  {
    if (SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostname, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK) == ADDON_STATUS_NEED_RESTART)
    {
      SetConnection(false);
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (settingName == "port")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_port, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "pin")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_PIN, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "remoteaccess")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_remoteAccess, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "showradio")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRadio, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "backendresume")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_backendResume, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "guideartwork")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_downloadGuideArtwork, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "guideartworkportrait")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_guideArtPortrait, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "castcrew")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_castcrew, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "recordingsize")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRecordingSize, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "diskspace")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_diskSpace, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "flattenrecording")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_flattenRecording, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "ignorepadding")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_ignorePadding, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "separateseasons")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_separateSeasons, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "kodilook")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_kodiLook, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "genrestring")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_genreString, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "host_mac")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostMACAddress, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "livestreamingmethod5")
    return SetEnumSetting<eStreamingMethod, ADDON_STATUS>(settingName, settingValue, m_liveStreamingMethod, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
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

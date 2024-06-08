/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#include "InstanceSettings.h"
#include <kodi/Filesystem.h>
#include "uri.h"

#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>

using namespace NextPVR;
using namespace NextPVR::utilities;

const std::string connectionFlag = "connection.flag";

InstanceSettings::InstanceSettings(kodi::addon::IAddonInstance& instance, const kodi::addon::IInstanceInfo& instanceInfo, bool first) :
  m_instance(instance),
  m_instanceInfo(instanceInfo),
  m_instancePriority(first)
{
  m_instanceNumber = m_instanceInfo.GetNumber();
  m_instanceDirectory = kodi::tools::StringUtils::Format("special://profile/addon_data/pvr.nextpvr/%d/", m_instanceNumber);
  ReadFromAddon();
}

/***************************************************************************
 * PVR settings
 **************************************************************************/
void InstanceSettings::ReadFromAddon()
{
  std::string buffer;

  /* Connection settings */
  /***********************/

  std::string protocol = ReadStringSetting("hostprotocol", DEFAULT_PROTOCOL);

  m_hostname = ReadStringSetting("host", DEFAULT_HOST);
  uri::decode(m_hostname);

  m_port = ReadIntSetting("port", DEFAULT_PORT);

  m_PIN = ReadStringSetting("pin", DEFAULT_PIN);

  sprintf(m_urlBase, "%s://%.255s:%d", protocol.c_str(), m_hostname.c_str(), m_port);

  m_enableWOL = ReadBoolSetting("wolenable", false);
  m_hostMACAddress = ReadStringSetting("host_mac", "");
  if (m_enableWOL)
  {
    if (m_hostMACAddress.empty())
      m_enableWOL = false;
    else if (m_hostname == "127.0.0.1" || m_hostname == "localhost" || m_hostname == "::1")
      m_enableWOL = false;
  }

  m_timeoutWOL = ReadIntSetting("woltimeout", 20);

  m_remoteAccess = ReadBoolSetting("remoteaccess", false);

  m_liveStreamingMethod = ReadEnumSetting<eStreamingMethod>("livestreamingmethod5", DEFAULT_LIVE_STREAM);

  m_flattenRecording = ReadBoolSetting("flattenrecording", false);

  m_separateSeasons = ReadBoolSetting("separateseasons", false);

  m_showRoot = ReadBoolSetting("showroot", false);

  m_prebuffer5 = ReadIntSetting("prebuffer5", 0);

  m_liveChunkSize = ReadIntSetting("chunklivetv", 64);

  m_chunkRecording = ReadIntSetting("chunkrecording", 32);

  m_ignorePadding = ReadBoolSetting("ignorepadding", true);

  m_resolution = ReadStringSetting("resolution", "720");

  m_accessLevel = ReadIntSetting("accesscontrol", ACCESS_RECORDINGS | ACCESS_RECORDINGS_DELETE | ACCESS_RECORDINGS_DELETE);

  m_showRadio = ReadBoolSetting("showradio", true);

  m_backendResume = ReadBoolSetting("backendresume", true);

  m_connectionConfirmed = kodi::vfs::FileExists(m_instanceDirectory + connectionFlag);

  if (m_PIN != "0000" && m_remoteAccess)
  {
    m_downloadGuideArtwork = false;
    m_sendSidWithMetadata = true;
  }  else {
    m_downloadGuideArtwork = ReadBoolSetting("guideartwork" ,DEFAULT_GUIDE_ARTWORK);
    m_sendSidWithMetadata = false;
  }

  m_guideArtPortrait = ReadBoolSetting("guideartworkportrait", false);

  m_genreString = ReadBoolSetting("genrestring", false);

  m_showRecordingSize = ReadBoolSetting("recordingsize", false);

  m_diskSpace = ReadStringSetting("diskspace", "Default");

  m_transcodedTimeshift = ReadBoolSetting("ffmpegdirect", false);

  m_castcrew = ReadBoolSetting("castcrew", false);

  m_useLiveStreams = ReadBoolSetting("uselivestreams", false);

  if (m_instanceNumber != ReadIntSetting("instance", 0))
  {
    m_instance.SetInstanceSettingInt("instance", m_instanceNumber);
  }

  m_instanceName = ReadStringSetting("kodi_addon_instance_name",  "Unknown");

  m_allChannels = ReadBoolSetting("instanceallgroup", false);

  m_addChannelInstance = ReadBoolSetting("instancechannel", false);

  m_comskip = ReadBoolSetting("comskip", true);

  m_priorityRegion = ReadStringSetting("certcountry", "US");

  enum eHeartbeat m_heartbeat = ReadEnumSetting<eHeartbeat>("heartbeat", eHeartbeat::Default);

  if (m_heartbeat == eHeartbeat::Default)
    m_heartbeatInterval = DEFAULT_HEARTBEAT;
  else if (m_heartbeat == eHeartbeat::FiveMinutes)
    m_heartbeatInterval = 300;
  else if (m_heartbeat == eHeartbeat::Hourly)
    m_heartbeatInterval = 7200;
  else if (m_heartbeat == eHeartbeat::None)
    m_heartbeatInterval = std::numeric_limits<time_t>::max();

  if (m_accessLevel == ACCESS_NONE)
    m_heartbeatInterval = std::numeric_limits<time_t>::max();

  /* Log the current settings for debugging purposes */
  kodi::Log(ADDON_LOG_DEBUG, "settings: host='%s', port=%i, instance=%d, mac=%4.4s...", m_hostname.c_str(), m_port, m_instanceNumber, m_hostMACAddress.c_str());

}

ADDON_STATUS InstanceSettings::ReadBackendSettings(tinyxml2::XMLDocument& settingsDoc)
{
  // check server version
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
      m_instance.SetInstanceSettingString("host_mac", macAddress);
    }
  }
  return ADDON_STATUS_OK;
}

void InstanceSettings::SetConnection(bool status)
{
  if (status == true)
  {
      kodi::vfs::CFile outputFile;
      outputFile.OpenFileForWrite(m_instanceDirectory + connectionFlag);
      m_connectionConfirmed = true;
  }
  else
  {
    kodi::vfs::RemoveDirectory(m_instanceDirectory);
    m_connectionConfirmed = false;
  }
}

bool InstanceSettings::CheckInstanceSettings()
{
  const std::string instanceFile = kodi::tools::StringUtils::Format("special://profile/addon_data/pvr.nextpvr/instance-settings-%d.xml", m_instanceNumber);
  bool instanceExists = kodi::vfs::FileExists(instanceFile);
  if (!instanceExists)
  {
    // instance xml deleted by Addon core remove cache for this instance.
    kodi::Log(ADDON_LOG_INFO, "Removing instance cache %s", m_instanceDirectory.c_str());
    SetConnection(false);
  }
  return instanceExists;
}

void InstanceSettings::UpdateServerPort(std::string hostname, int port)
{
  if (hostname != DEFAULT_HOST || port != DEFAULT_PORT )
  {
    m_instance.SetInstanceSettingString("host", hostname);
    m_hostname = hostname;
    m_instance.SetInstanceSettingInt("port", port);
    m_port = port;
    // might reset https but not included in discovery anyway
    sprintf(m_urlBase, "http://%.255s:%d",m_hostname.c_str(), m_port);
  }
};

void InstanceSettings::SetVersionSpecificSettings()
{

  // No version specific setting

}

std::string InstanceSettings::ReadStringSetting(const std::string& key,
                                                const std::string& def) const
{
  std::string value;
  if (m_instance.CheckInstanceSettingString(key, value))
    return value;

  return def;
}

int InstanceSettings::ReadIntSetting(const std::string& key, int def) const
{
  int value;
  if (m_instance.CheckInstanceSettingInt(key, value))
    return value;

  return def;
}

bool InstanceSettings::ReadBoolSetting(const std::string& key, bool def) const
{
  bool value;
  if (m_instance.CheckInstanceSettingBoolean(key, value))
    return value;

  return def;
}

ADDON_STATUS InstanceSettings::SetValue(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
  //Connection
  //To-do check logic don't want to cause a restart after the first time discovery

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
  else if (settingName == "accesscontrol")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_accessLevel, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
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
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_flattenRecording, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "ignorepadding")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_ignorePadding, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "separateseasons")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_separateSeasons, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "showroot")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRoot, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
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
  else if (settingName == "instancechannel")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_addChannelInstance, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "instancegroup")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_allChannels, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "heartbeat")
    return SetEnumSetting<eHeartbeat, ADDON_STATUS>(settingName, settingValue, m_heartbeat, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "certcountry")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_priorityRegion, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  return ADDON_STATUS_OK;
}

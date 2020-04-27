/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#include "Settings.h"
#include "BackendRequest.h"


#include <kodi/util/XMLUtils.h>
#include <p8-platform/util/StringUtils.h>
#include <tinyxml.h>
#include "uri.h"

using namespace std;
using namespace ADDON;
using namespace NextPVR;

/***************************************************************************
 * PVR settings
 **************************************************************************/
void Settings::ReadFromAddon()
{
  char buffer[1024];

  /* Connection settings */
  /***********************/
  if (XBMC->GetSetting("host", buffer))
  {
    m_hostname = buffer;
    uri::decode(m_hostname);
  }
  else
  {
    m_hostname = DEFAULT_HOST;
  }

  /* Read setting "port" from settings.xml */
  if (!XBMC->GetSetting("port", &m_port))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'port' setting, falling back to '8866' as default");
    m_port = DEFAULT_PORT;
  }
    /* Read setting "pin" from settings.xml */

  if (!XBMC->GetSetting("pin", buffer))
    m_PIN = DEFAULT_PIN;
  else
    m_PIN = buffer;

  if (XBMC->GetSetting("host_mac", buffer))
    m_hostMACAddress = buffer;

  if (m_hostMACAddress.empty())
    m_enableWOL = false;
  else if (!XBMC->GetSetting("wolenable", &m_enableWOL))
    m_enableWOL = false;
  else if (m_hostname == "127.0.0.1" || m_hostname == "localhost" || m_hostname == "::1")
    m_enableWOL = false;

  if (!XBMC->GetSetting("woltimeout", &m_timeoutWOL))
    m_timeoutWOL = 20;

  if (!XBMC->GetSetting("guideartwork", &m_downloadGuideArtwork))
    m_downloadGuideArtwork = DEFAULT_GUIDE_ARTWORK;

  if (!XBMC->GetSetting("remoteaccess", &m_remoteAccess))
    m_remoteAccess = false;

  if (!XBMC->GetSetting("flattenrecording", &m_flattenRecording))
    m_flattenRecording = false;

  if (!XBMC->GetSetting("reseticons", &m_resetIcons))
    m_resetIcons = false;

  if (!XBMC->GetSetting("kodilook", &m_kodiLook))
    m_kodiLook = false;

  if (!XBMC->GetSetting("prebuffer", &m_prebuffer))
    m_prebuffer = 8;

  if (!XBMC->GetSetting("prebuffer5", &m_prebuffer5))
    m_prebuffer5 = 0;

  if (!XBMC->GetSetting("chunklivetv", &m_liveChunkSize))
    m_liveChunkSize = 64;

  if (!XBMC->GetSetting("chunkrecording", &m_chunkRecording))
    m_chunkRecording = 32;

  if (XBMC->GetSetting("resolution", &buffer))
    m_resolution = buffer;
  else
    m_resolution = "720";

/* Log the current settings for debugging purposes */
  XBMC->Log(LOG_DEBUG, "settings: host='%s', port=%i, mac=%4.4s...", m_hostname.c_str(), m_port, m_hostMACAddress.c_str());

}

ADDON_STATUS Settings::ReadBackendSettings()
{
  // check server version
  std::string settings;
  if (NextPVR::m_backEnd->DoRequest("/service?method=setting.list", settings) == HTTP_OK)
  {
    TiXmlDocument settingsDoc;
    if (settingsDoc.Parse(settings.c_str()) != NULL)
    {
      //dump_to_log(&settingsDoc, 0);
      if (XMLUtils::GetInt(settingsDoc.RootElement(), "NextPVRVersion", m_backendVersion))
      {
        // NextPVR server
        XBMC->Log(LOG_INFO, "NextPVR version: %d", m_backendVersion);

        // is the server new enough
        if (m_backendVersion < 40204)
        {
          XBMC->Log(LOG_ERROR, "Your NextPVR version '%d' is too old. Please upgrade to '%s' or higher!", m_backendVersion, NEXTPVRC_MIN_VERSION_STRING);
          XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30050));
          XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30051), NEXTPVRC_MIN_VERSION_STRING);
          return ADDON_STATUS_PERMANENT_FAILURE;
        }
      }

      // load padding defaults
      m_defaultPrePadding = 1;
      XMLUtils::GetInt(settingsDoc.RootElement(), "PrePadding", m_defaultPrePadding);

      m_defaultPostPadding = 2;
      XMLUtils::GetInt(settingsDoc.RootElement(), "PostPadding", m_defaultPostPadding);

      m_showNew = false;
      XMLUtils::GetBoolean(settingsDoc.RootElement(),"ShowNewInGuide",m_showNew);

      std::string recordingDirectories;
      if (XMLUtils::GetString(settingsDoc.RootElement(),"RecordingDirectories",recordingDirectories))
      {
        m_recordingDirectories = StringUtils::Split(recordingDirectories, ",", 0);
        /*
        vector<std::string> directories = split(recordingDirectories, ",", false);
        for (size_t i = 0; i < directories.size(); i++)
        {
          m_recordingDirectories.push_back(directories[i]);
        }
        */
      }

      int serverTimestamp;
      if (XMLUtils::GetInt(settingsDoc.RootElement(), "TimeEpoch", serverTimestamp))
      {
        m_serverTimeOffset = time(nullptr) - serverTimestamp;
        XBMC->Log(LOG_INFO, "Server time offset in seconds: %d", m_serverTimeOffset);
      }

      if (XMLUtils::GetInt(settingsDoc.RootElement(), "SlipSeconds", m_timeshiftBufferSeconds))
        XBMC->Log(LOG_INFO, "time shift buffer in seconds == %d\n", m_timeshiftBufferSeconds);

      std::string serverMac;
      if (XMLUtils::GetString(settingsDoc.RootElement(), "ServerMAC", serverMac))
      {
        // only available from Windows backend
        char rawMAC[13];
        PVR_STRCPY(rawMAC, serverMac.c_str());
        if (strlen(rawMAC) == 12)
        {
          char mac[18];
          sprintf(mac, "%2.2s:%2.2s:%2.2s:%2.2s:%2.2s:%2.2s", rawMAC, &rawMAC[2], &rawMAC[4], &rawMAC[6], &rawMAC[8], &rawMAC[10]);
          XBMC->Log(LOG_DEBUG, "Server MAC addres %4.4s...", mac);
          std::string smac = mac;
          if (m_hostMACAddress != smac)
          {
            SaveSettings("host_mac", smac);
          }
        }
      }
    }
  }
  return ADDON_STATUS_OK;
}

void Settings::SetVersionSpecificSettings()
{
  m_liveStreamingMethod = DEFAULT_LIVE_STREAM;

  if (XBMC->GetSetting("livestreamingmethod", &m_liveStreamingMethod))
  {
    // has v4 setting
    if (m_backendVersion < 50000)
    {
      // previous Matrix clients had a transcoding option
      if (m_liveStreamingMethod == eStreamingMethod::Transcoded)
      {
        m_liveStreamingMethod = eStreamingMethod::RealTime;
        XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30051), "5");
      }
    }
    else if (m_backendVersion < 50002)
    {
      m_liveStreamingMethod = eStreamingMethod::RealTime;
      XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30051), "5.0.2+");
    }
    else
    {
      // check for new v5 setting with no settings.xml
      eStreamingMethod oldMethod = m_liveStreamingMethod;
      XBMC->GetSetting("livestreamingmethod5", &m_liveStreamingMethod);

      if (m_liveStreamingMethod == eStreamingMethod::Default)
        m_liveStreamingMethod = oldMethod;

      if (m_liveStreamingMethod == RollingFile || m_liveStreamingMethod == Timeshift)
        m_liveStreamingMethod = eStreamingMethod::ClientTimeshift;

    }
  }

  if (m_backendVersion >= 50000)
  {
    m_sendSidWithMetadata = false;
    bool remote;
    if (m_PIN != "0000" && m_remoteAccess)
    {
      m_downloadGuideArtwork = false;
      m_sendSidWithMetadata = true;
    }

    if (!XBMC->GetSetting("guideartworkportrait", &m_guideArtPortrait))
      m_guideArtPortrait = false;

    if (!XBMC->GetSetting("recordingsize", &m_showRecordingSize))
      m_showRecordingSize = false;
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

  char *settings = XBMC->TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/settings.xml");
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
        if ( childNode->QueryStringAttribute("id", &key_value) == TIXML_SUCCESS)
        {
          if (key_value == name)
          {
            if (childNode->FirstChild() != NULL)
            {
              childNode->FirstChild()->SetValue( value );
              found = true;
              break;
            }
            return false;
          }
        }
      }
      if (found == false)
      {
        TiXmlElement *newSetting = new TiXmlElement("setting");
        TiXmlText *newvalue = new TiXmlText(value);
        newSetting->SetAttribute("id", name);
        newSetting->LinkEndChild(newvalue);
        rootNode->LinkEndChild(newSetting);
      }
      doc.SaveFile(settings);
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Error loading settings.xml %s", settings);
  }
  XBMC->FreeString(settings);
  return true;
}



ADDON_STATUS Settings::SetValue(const std::string& settingName, const void* settingValue)
{
  //Connection
  if (settingName == "host")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostname, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "port")
    return SetSetting<int, ADDON_STATUS>(settingName, settingValue, m_port, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "pin")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_PIN, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "remoteaccess")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_remoteAccess, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "guideartwork")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_downloadGuideArtwork, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "guideartworkportrait")
    return SetSetting<bool,ADDON_STATUS>(settingName, settingValue, m_guideArtPortrait, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "recordingsize")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_showRecordingSize, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "flattenrecording")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_flattenRecording, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "reseticons")
    return SetSetting<bool, ADDON_STATUS>(settingName, settingValue, m_resetIcons, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "kodilook")
    return SetSetting<bool ,ADDON_STATUS>(settingName, settingValue, m_kodiLook, ADDON_STATUS_NEED_SETTINGS, ADDON_STATUS_OK);
  else if (settingName == "host_mac")
    return SetStringSetting<ADDON_STATUS>(settingName, settingValue, m_hostMACAddress, ADDON_STATUS_OK, ADDON_STATUS_OK);
  else if (settingName == "livestreamingmethod" && g_client && m_backendVersion < 50000)
    return SetSetting<eStreamingMethod, ADDON_STATUS>(settingName, settingValue, m_liveStreamingMethod, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
  else if (settingName == "livestreamingmethod5" && g_client && m_backendVersion >= 50000 && *static_cast<const eStreamingMethod*>(settingValue) != Default)
    return SetSetting<eStreamingMethod, ADDON_STATUS>(settingName, settingValue, m_liveStreamingMethod, ADDON_STATUS_NEED_RESTART, ADDON_STATUS_OK);
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
  return ADDON_STATUS_OK;
}

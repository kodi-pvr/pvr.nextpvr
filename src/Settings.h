/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "client.h"
#include <string>
#include <kodi/AddonBase.h>
#include <p8-platform/util/StringUtils.h>


using namespace ADDON;

namespace NextPVR
{
#define PVRCLIENT_NEXTPVR_VERSION_STRING "1.0.0.0"
#define NEXTPVRC_MIN_VERSION_STRING "4.2.4"
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8866
#define DEFAULT_PIN "0000"
#define DEFAULT_RADIO true
#define DEFAULT_USE_TIMESHIFT false
#define DEFAULT_GUIDE_ARTWORK false
#define DEFAULT_LIVE_STREAM RealTime

  enum eStreamingMethod
  {
    Default = -1,
    Timeshift = 0,
    RollingFile = 1,
    RealTime = 2,
    Transcoded = 3,
    ClientTimeshift = 4
  };

  enum eGuideArt
  {
    Portrait = 0,
    Landscape = 1
  };

  class Settings
  {
  public:

    /**
     * Singleton getter for the instance
     */
    static Settings& GetInstance()
    {
      static Settings settings;
      return settings;
    }
    ADDON_STATUS ReadBackendSettings();
    void SetVersionSpecificSettings();
    bool SaveSettings(std::string name, std::string value);
    void UpdateServerPort(std::string host, int port)
    {
      m_hostname = host;
      m_port = port;
    };

    void ReadFromAddon();
    ADDON_STATUS SetValue(const std::string& settingName, const void* settingValue);

    //Connection
    std::string m_hostname = DEFAULT_HOST;
    int m_port = 8866;
    bool m_remoteAccess = false;
    int m_serverTimeOffset = 0;
    std::string m_hostMACAddress = "";
    std::string m_PIN = "";
    bool m_enableWOL = false;
    int m_timeoutWOL = 0;

    //General
    int m_backendVersion = 0;

    //Channel
    bool m_showRadio = true;

    //EPG
    bool m_showNew = false;
    bool m_downloadGuideArtwork = false;
    bool m_sendSidWithMetadata = false;
    bool m_guideArtPortrait = false;

    //Recordings
    bool m_showRecordingSize = false;
    bool m_flattenRecording = false;
    bool m_kodiLook = false;
    int m_chunkRecording = 32;

    //Timers
    int m_defaultPrePadding = 0;
    int m_defaultPostPadding = 0;
    std::vector<std::string> m_recordingDirectories;

    //Timeshift
    int m_timeshiftBufferSeconds = 1200;
    eStreamingMethod m_liveStreamingMethod = RealTime;
    int m_liveChunkSize = 64;
    //int m_prebuffer;
    int m_prebuffer = 8;
    int m_prebuffer5 = 0;
    std::string m_resolution = "720";

  private:

    Settings() = default;

    Settings(Settings const&) = delete;
    void operator=(Settings const&) = delete;

    template<typename T, typename V>
    V SetSetting(const std::string& settingName, const void* settingValue, T& currentValue, V returnValueIfChanged, V defaultReturnValue)
    {
      T newValue = *static_cast<const T*>(settingValue);
      if (newValue != currentValue)
      {
        std::string formatString = "%s - Changed Setting '%s' from %d to %d";
        if (std::is_same<T, float>::value)
          formatString = "%s - Changed Setting '%s' from %f to %f";
        XBMC->Log(LOG_INFO, formatString.c_str(), __FUNCTION__, settingName.c_str(), currentValue, newValue);
        currentValue = newValue;
        return returnValueIfChanged;
      }

      return defaultReturnValue;
    };

    template<typename V>
    V SetStringSetting(const std::string& settingName, const void* settingValue, std::string& currentValue, V returnValueIfChanged, V defaultReturnValue)
    {
      const std::string strSettingValue = static_cast<const char*>(settingValue);

      if (strSettingValue != currentValue)
      {
        currentValue = strSettingValue;
        return returnValueIfChanged;
      }

      return defaultReturnValue;
    }
  };
} //namespace NextPVR

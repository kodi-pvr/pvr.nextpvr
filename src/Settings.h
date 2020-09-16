/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "addon.h"
#include <string>
#include <kodi/AddonBase.h>
#include <p8-platform/util/StringUtils.h>

namespace NextPVR
{

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

  const static std::string PVRCLIENT_NEXTPVR_VERSION_STRING = "1.0.0.0";
  constexpr char NEXTPVRC_MIN_VERSION_STRING[] = "4.2.4";
  const static std::string DEFAULT_PROTOCOL = "http";
  const static std::string DEFAULT_HOST = "127.0.0.1";
  constexpr int DEFAULT_PORT = 8866;
  const static std::string DEFAULT_PIN = "0000";
  constexpr bool DEFAULT_RADIO = true;
  constexpr bool DEFAULT_USE_TIMESHIFT = false;
  constexpr bool DEFAULT_GUIDE_ARTWORK = false;
  constexpr eStreamingMethod DEFAULT_LIVE_STREAM = RealTime;

  class ATTRIBUTE_HIDDEN Settings
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
    bool GetConnection();
    void SetConnection(bool status);
    void SetVersionSpecificSettings();
    void UpdateServerPort(std::string host, int port)
    {
      m_hostname = host;
      m_port = port;
    };

    void ReadFromAddon();
    ADDON_STATUS SetValue(const std::string& settingName, const kodi::CSettingValue& settingValue);

    //Connection
    std::string m_hostname = DEFAULT_HOST;
    char m_urlBase[512]{ 0 };
    int m_port = 8866;
    bool m_remoteAccess = false;
    time_t m_serverTimeOffset = 0;
    std::string m_hostMACAddress = "";
    std::string m_PIN = "";
    bool m_enableWOL = false;
    int m_timeoutWOL = 0;
    bool m_connectionConfirmed = false;

    //General
    int m_backendVersion = 0;

    //Channel
    bool m_showRadio = true;

    //EPG
    bool m_showNew = false;
    bool m_downloadGuideArtwork = false;
    bool m_sendSidWithMetadata = false;
    bool m_guideArtPortrait = false;
    bool m_genreString = false;

    //Recordings
    bool m_showRecordingSize = false;
    bool m_flattenRecording = false;
    bool m_separateSeasons = true;
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
    bool m_transcodedTimeshift = false;

  private:

    Settings() = default;

    Settings(Settings const&) = delete;
    void operator=(Settings const&) = delete;

    template<typename T, typename V>
    V SetSetting(const std::string& settingName, const kodi::CSettingValue& settingValue, T& currentValue, V returnValueIfChanged, V defaultReturnValue)
    {
      T newValue;
      if (std::is_same<T, float>::value)
        newValue = static_cast<T>(settingValue.GetFloat());
      else if (std::is_same<T, bool>::value)
        newValue = static_cast<T>(settingValue.GetBoolean());
      else if (std::is_same<T, unsigned int>::value)
        newValue = static_cast<T>(settingValue.GetUInt());
      else if (std::is_same<T, int>::value)
        newValue = static_cast<T>(settingValue.GetInt());

      if (newValue != currentValue)
      {
        std::string formatString = "%s - Changed Setting '%s' from %d to %d";
        if (std::is_same<T, float>::value)
          formatString = "%s - Changed Setting '%s' from %f to %f";
        kodi::Log(ADDON_LOG_INFO, formatString.c_str(), __FUNCTION__, settingName.c_str(), currentValue, newValue);
        currentValue = newValue;
        return returnValueIfChanged;
      }

      return defaultReturnValue;
    };

    template<typename T, typename V>
    V SetEnumSetting(const std::string& settingName, const kodi::CSettingValue& settingValue, T& currentValue, V returnValueIfChanged, V defaultReturnValue)
    {
      T newValue = settingValue.GetEnum<T>();
      if (newValue != currentValue)
      {
        kodi::Log(ADDON_LOG_INFO, "%s - Changed Setting '%s' from %d to %d", __FUNCTION__, settingName.c_str(), currentValue, newValue);
        currentValue = newValue;
        return returnValueIfChanged;
      }

      return defaultReturnValue;
    };

    template<typename V>
    V SetStringSetting(const std::string& settingName, const kodi::CSettingValue& settingValue, std::string& currentValue, V returnValueIfChanged, V defaultReturnValue)
    {
      const std::string strSettingValue = settingValue.GetString();

      if (strSettingValue != currentValue)
      {
        currentValue = strSettingValue;
        return returnValueIfChanged;
      }

      return defaultReturnValue;
    }
  };
} //namespace NextPVR

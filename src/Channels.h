/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "BackendRequest.h"
#include "tinyxml.h"

using namespace ADDON;

namespace NextPVR
{

  class Channels
  {

  public:
    /**
       * Singleton getter for the instance
       */
    static Channels& GetInstance()
    {
      static Channels channels;
      return channels;
    }

    /* Channel handling */
    int GetNumChannels(void);
    PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio);

    /* Channel group handling */
    int GetChannelGroupsAmount(void);
    PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio);
    PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group);
    bool IsChannelAPlugin(int uid);
    void LoadLiveStreams();
    std::map<int, std::string> m_liveStreams;
    std::string GetChannelIconFileName(int channelID);
    void DeleteChannelIcon(int channelID);
    void DeleteChannelIcons();
    PVR_RECORDING_CHANNEL_TYPE GetChannelType(unsigned int uid);
    std::map<int, std::pair<bool, bool>> m_channelDetails;

  private:
    Channels() = default;

    Channels(Channels const&) = delete;
    void operator=(Channels const&) = delete;

    std::string GetChannelIcon(int channelID);
    Settings& m_settings = Settings::GetInstance();
    Request& m_request = Request::GetInstance();
  };
} // namespace NextPVR

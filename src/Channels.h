/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "BackendRequest.h"
#include <kodi/addon-instance/PVR.h>
#include "tinyxml.h"

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
    int GetNumChannels();

    PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results);
    /* Channel group handling */
    PVR_ERROR GetChannelGroupsAmount(int& amount);
    PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results);
    PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results);
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

/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "BackendRequest.h"
#include <kodi/addon-instance/PVR.h>
#include <unordered_set>

namespace NextPVR
{

  class ATTR_DLL_LOCAL Channels
  {

  public:

    Channels(const std::shared_ptr<InstanceSettings>& settings, Request& request);

    /* Channel handling */
    int GetNumChannels();

    PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results);
    /* Channel group handling */
    PVR_ERROR GetChannelGroupsAmount(int& amount);
    PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results);
    PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results);
    const std::string GetAllChannelsGroupName(bool radio);
    bool IsChannelAPlugin(int uid);
    void LoadLiveStreams();
    std::map<int, std::string> m_liveStreams;
    std::string GetChannelIconFileName(int channelID);
    void DeleteChannelIcon(int channelID);
    void DeleteChannelIcons();
    PVR_RECORDING_CHANNEL_TYPE GetChannelType(unsigned int uid);
    std::map<int, std::pair<bool, bool>> m_channelDetails;
    std::unordered_set<std::string> m_tvGroups;
    std::unordered_set<std::string> m_radioGroups;

  private:
    Channels() = default;

    Channels(Channels const&) = delete;
    void operator=(Channels const&) = delete;

    std::string GetChannelIcon(int channelID);
    const std::shared_ptr<InstanceSettings> m_settings;
    Request& m_request;
  };
} // namespace NextPVR

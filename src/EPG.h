/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "BackendRequest.h"
#include <kodi/addon-instance/PVR.h>
#include "Channels.h"
#include "Recordings.h"

namespace NextPVR
{
  const int YEAR_NOT_SET = -1;
  class ATTR_DLL_LOCAL EPG
  {
  public:
    EPG(const std::shared_ptr<InstanceSettings>& settings, Request& request, Recordings& recordings, Channels& channels);
    PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results);

  private:
    EPG() = default;
    EPG(EPG const&) = delete;
    void operator=(EPG const&) = delete;

    const std::shared_ptr<InstanceSettings> m_settings;
    Request& m_request;
    Recordings& m_recordings;
    Channels& m_channels;
  };
} // namespace NextPVR

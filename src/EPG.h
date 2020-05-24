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
#include "Channels.h"
#include "Recordings.h"
#include "Timers.h"

using namespace ADDON;

namespace NextPVR
{
  class EPG
  {
  public:
    /**
       * Singleton getter for the instance
       */
    static EPG& GetInstance()
    {
      static EPG epg;
      return epg;
    }

    PVR_ERROR GetEpg(ADDON_HANDLE handle, int iChannelUid, time_t iStart = 0, time_t iEnd = 0);

  private:
    EPG() = default;

    EPG(EPG const&) = delete;
    void operator=(EPG const&) = delete;

    Settings& m_settings = Settings::GetInstance();
    Request& m_request = Request::GetInstance();
    Timers& m_timers = Timers::GetInstance();
    Recordings& m_recordings = Recordings::GetInstance();
    Channels& m_channels = Channels::GetInstance();
  };
} // namespace NextPVR

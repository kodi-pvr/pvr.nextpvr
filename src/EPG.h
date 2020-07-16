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
#include "Channels.h"
#include "Recordings.h"
#include "Timers.h"



namespace NextPVR
{
  class ATTRIBUTE_HIDDEN EPG
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
    PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results);

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

/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "client.h"
#include "Channels.h"
#include "Recordings.h"
#include "Settings.h"

using namespace ADDON;

namespace NextPVR
{

  constexpr int PVR_MENUHOOK_CHANNEL_DELETE_SINGLE_CHANNEL_ICON=101;
  constexpr int PVR_MENUHOOK_RECORDING_FORGET_RECORDING=401;
  constexpr int PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS=601;
  constexpr int PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS=602;
  constexpr int PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS=603;


  class MenuHook
  {

  public:
    /**
       * Singleton getter for the instance
       */
    static MenuHook& GetInstance()
    {
      static MenuHook menuhook;
      return menuhook;
    }

    PVR_ERROR CallMenuHook(const PVR_MENUHOOK& menuhook, const PVR_MENUHOOK_DATA& item);
    void ConfigureMenuHook();


  private:
    MenuHook() = default;

    MenuHook(MenuHook const&) = delete;
    void operator=(MenuHook const&) = delete;

    Channels& m_channels = Channels::GetInstance();
    Recordings& m_recordings = Recordings::GetInstance();
    Settings& m_settings = Settings::GetInstance();
  };
} // namespace NextPVR

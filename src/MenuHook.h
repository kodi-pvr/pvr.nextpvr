/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "Channels.h"
#include "Recordings.h"
#include "Settings.h"

namespace NextPVR
{

  constexpr int PVR_MENUHOOK_CHANNEL_DELETE_SINGLE_CHANNEL_ICON = 101;
  constexpr int PVR_MENUHOOK_RECORDING_FORGET_RECORDING = 401;
  constexpr int PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS = 601;
  constexpr int PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS = 602;
  constexpr int PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS = 603;
  constexpr int PVR_MENUHOOK_SETTING_SEND_WOL = 604;
  constexpr int PVR_MENUHOOK_SETTING_OPEN_SETTINGS = 605;

  class ATTRIBUTE_HIDDEN MenuHook
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

    PVR_ERROR CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRChannel& item);
    PVR_ERROR CallRecordingsMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRRecording& item);
    PVR_ERROR CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook);

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

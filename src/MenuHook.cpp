/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "MenuHook.h"

using namespace NextPVR;

PVR_ERROR MenuHook::CallMenuHook(const PVR_MENUHOOK& menuhook, const PVR_MENUHOOK_DATA& item)
{
  if (item.cat == PVR_MENUHOOK_CHANNEL && menuhook.iHookId == PVR_MENUHOOK_CHANNEL_DELETE_SINGLE_CHANNEL_ICON)
  {
    m_channels.DeleteChannelIcon(item.data.channel.iUniqueId);
    PVR->TriggerChannelUpdate();
  }
  else if (item.cat == PVR_MENUHOOK_RECORDING && menuhook.iHookId == PVR_MENUHOOK_RECORDING_FORGET_RECORDING)
  {
    m_recordings.ForgetRecording(item.data.recording);
  }
  else if (item.cat == PVR_MENUHOOK_SETTING && menuhook.iHookId == PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS)
  {
    m_channels.DeleteChannelIcons();
    PVR->TriggerChannelUpdate();
  }
  else if (item.cat == PVR_MENUHOOK_SETTING && menuhook.iHookId == PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS)
  {
    PVR->TriggerChannelUpdate();
  }
  else if (item.cat == PVR_MENUHOOK_SETTING && menuhook.iHookId == PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS)
  {
    PVR->TriggerChannelGroupsUpdate();
  }
  return PVR_ERROR_NO_ERROR;
}

void MenuHook::ConfigureMenuHook()
{
  PVR_MENUHOOK menuHook;
  menuHook = {0};
  menuHook.category = PVR_MENUHOOK_CHANNEL;
  menuHook.iHookId = PVR_MENUHOOK_CHANNEL_DELETE_SINGLE_CHANNEL_ICON;
  menuHook.iLocalizedStringId = 30183;
  PVR->AddMenuHook(&menuHook);

  menuHook = {0};
  menuHook.category = PVR_MENUHOOK_SETTING;
  menuHook.iHookId = PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS;
  menuHook.iLocalizedStringId = 30170;
  PVR->AddMenuHook(&menuHook);

  menuHook = {0};
  menuHook.category = PVR_MENUHOOK_SETTING;
  menuHook.iHookId = PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS;
  menuHook.iLocalizedStringId = 30185;
  PVR->AddMenuHook(&menuHook);

  menuHook = {0};
  menuHook.category = PVR_MENUHOOK_SETTING;
  menuHook.iHookId = PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS;
  menuHook.iLocalizedStringId = 30186;
  PVR->AddMenuHook(&menuHook);

  if (m_settings.m_backendVersion >= 50000)
  {
    menuHook = {0};
    menuHook.category = PVR_MENUHOOK_RECORDING;
    menuHook.iHookId = PVR_MENUHOOK_RECORDING_FORGET_RECORDING;
    menuHook.iLocalizedStringId = 30184;
    PVR->AddMenuHook(&menuHook);
  }
}

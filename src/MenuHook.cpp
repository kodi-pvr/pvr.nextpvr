/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "MenuHook.h"
#include "pvrclient-nextpvr.h"
#include <kodi/addon-instance/PVR.h>
#include <kodi/General.h>

using namespace NextPVR;

PVR_ERROR MenuHook::CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
  if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS)
  {
    m_channels.DeleteChannelIcons();
    g_pvrclient->TriggerChannelUpdate();
  }
  else if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS)
  {
    g_pvrclient->TriggerChannelUpdate();
  }
  else if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS)
  {
    g_pvrclient->TriggerChannelGroupsUpdate();
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR MenuHook::CallRecordingsMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRRecording& item)
{
  if (menuhook.GetHookId() == PVR_MENUHOOK_RECORDING_FORGET_RECORDING)
  {
    m_recordings.ForgetRecording(item);
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR MenuHook::CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRChannel& item)
{
  if (menuhook.GetHookId() == PVR_MENUHOOK_CHANNEL_DELETE_SINGLE_CHANNEL_ICON)
  {
    m_channels.DeleteChannelIcon(item.GetUniqueId());
  }

  return PVR_ERROR_NO_ERROR;
}

void MenuHook::ConfigureMenuHook()
{
  kodi::addon::PVRMenuhook menuHook;
  menuHook.SetCategory(PVR_MENUHOOK_CHANNEL);
  menuHook.SetHookId(PVR_MENUHOOK_CHANNEL_DELETE_SINGLE_CHANNEL_ICON);
  menuHook.SetLocalizedStringId(30183);
  g_pvrclient->AddMenuHook(menuHook);


  menuHook.SetCategory(PVR_MENUHOOK_SETTING);
  menuHook.SetHookId(PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS);
  menuHook.SetLocalizedStringId(30170);
  g_pvrclient->AddMenuHook(menuHook);

  menuHook.SetCategory(PVR_MENUHOOK_SETTING);
  menuHook.SetHookId(PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS);
  menuHook.SetLocalizedStringId(30185);
  g_pvrclient->AddMenuHook(menuHook);

  menuHook.SetCategory(PVR_MENUHOOK_SETTING);
  menuHook.SetHookId(PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS);
  menuHook.SetLocalizedStringId(30186);
  g_pvrclient->AddMenuHook(menuHook);

  if (m_settings.m_backendVersion >= 50000)
  {
    menuHook.SetCategory(PVR_MENUHOOK_RECORDING);
    menuHook.SetHookId(PVR_MENUHOOK_RECORDING_FORGET_RECORDING);
    menuHook.SetLocalizedStringId(30184);
    g_pvrclient->AddMenuHook(menuHook);
  }
}

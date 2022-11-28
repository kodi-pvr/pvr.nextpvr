/*
 *  Copyright (C) 2020-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "MenuHook.h"
#include "pvrclient-nextpvr.h"
#include <kodi/addon-instance/PVR.h>
#include <kodi/General.h>

using namespace NextPVR;
MenuHook::MenuHook(const std::shared_ptr<InstanceSettings>& settings, Recordings& recordings, Channels& channels, cPVRClientNextPVR& pvrclient) :
  m_settings(settings),
  m_recordings(recordings),
  m_channels(channels),
  m_pvrclient(pvrclient)
{
  ConfigureMenuHook();
}



PVR_ERROR MenuHook::CallSettingsMenuHook(const kodi::addon::PVRMenuhook& menuhook)
{
  if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS)
  {
    m_channels.DeleteChannelIcons();
    m_pvrclient.TriggerChannelUpdate();
  }
  else if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS)
  {
    m_pvrclient.TriggerChannelUpdate();
  }
  else if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS)
  {
    m_pvrclient.TriggerChannelGroupsUpdate();
  }
  else if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_SEND_WOL)
  {
    m_pvrclient.SendWakeOnLan();
  }
  else if (menuhook.GetHookId() == PVR_MENUHOOK_SETTING_OPEN_SETTINGS)
  {
    kodi::addon::OpenSettings();
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
  m_pvrclient.AddMenuHook(menuHook);


  menuHook.SetCategory(PVR_MENUHOOK_SETTING);
  menuHook.SetHookId(PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS);
  menuHook.SetLocalizedStringId(30170);
  m_pvrclient.AddMenuHook(menuHook);

  menuHook.SetCategory(PVR_MENUHOOK_SETTING);
  menuHook.SetHookId(PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS);
  menuHook.SetLocalizedStringId(30185);
  m_pvrclient.AddMenuHook(menuHook);

  menuHook.SetCategory(PVR_MENUHOOK_SETTING);
  menuHook.SetHookId(PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS);
  menuHook.SetLocalizedStringId(30186);
  m_pvrclient.AddMenuHook(menuHook);

  if (m_settings->m_enableWOL)
  {
    menuHook.SetCategory(PVR_MENUHOOK_SETTING);
    menuHook.SetHookId(PVR_MENUHOOK_SETTING_SEND_WOL);
    menuHook.SetLocalizedStringId(30195);
    m_pvrclient.AddMenuHook(menuHook);
  }
  if (m_settings->m_instanceNumber == 1)
  {
    menuHook.SetCategory(PVR_MENUHOOK_SETTING);
    menuHook.SetHookId(PVR_MENUHOOK_SETTING_OPEN_SETTINGS);
    menuHook.SetLocalizedStringId(30196);
    m_pvrclient.AddMenuHook(menuHook);
  }

  menuHook.SetCategory(PVR_MENUHOOK_RECORDING);
  menuHook.SetHookId(PVR_MENUHOOK_RECORDING_FORGET_RECORDING);
  menuHook.SetLocalizedStringId(30184);
  m_pvrclient.AddMenuHook(menuHook);
}

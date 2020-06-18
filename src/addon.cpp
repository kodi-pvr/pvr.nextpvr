/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "addon.h"
#include "pvrclient-nextpvr.h"
/* User adjustable settings are saved here.
 * Default values are defined inside addon.h
 * and exported to the other source files.
 */

/* Client member variables */
ADDON_STATUS m_CurStatus = ADDON_STATUS_UNKNOWN;
cPVRClientNextPVR *g_pvrclient = nullptr;

NextPVR::Settings& settings = NextPVR::Settings::GetInstance();

/***********************************************************
 * Standard AddOn related public library functions
 ***********************************************************/

//-- Create -------------------------------------------------------------------
// Called after loading of the dll, all steps to become Client functional
// must be performed here.
//-----------------------------------------------------------------------------

ADDON_STATUS CNextPVRAddon::Create()
{
  kodi::Log(ADDON_LOG_INFO, "Creating NextPVR PVR-Client");
  return ADDON_STATUS_OK;
}

ADDON_STATUS CNextPVRAddon::CreateInstance(int instanceType,
    const std::string& instanceID, KODI_HANDLE instance,
    const std::string& version, KODI_HANDLE& addonInstance)
{
  settings.ReadFromAddon();

  if (!kodi::vfs::DirectoryExists("special://userdata/addon_data/pvr.nextpvr/"))
  {
    Request& request = Request::GetInstance();
    request.OneTimeSetup();
  }

  /* Create connection to NextPVR KODI TV client */
  g_pvrclient = new cPVRClientNextPVR(*this, instance, version);
  m_CurStatus = g_pvrclient->Connect();

  if (m_CurStatus == ADDON_STATUS_OK)
  {
    addonInstance = g_pvrclient;
    m_usedInstances.emplace(std::make_pair(instanceID, g_pvrclient));
    g_pvrclient->m_menuhook.ConfigureMenuHook();
  }

  return m_CurStatus;
}

//-- Destroy ------------------------------------------------------------------
// Used during destruction of the client, all steps to do clean and safe Create
// again must be done.
//-----------------------------------------------------------------------------
void CNextPVRAddon::DestroyInstance(int instanceType,
                                    const std::string& instanceID,
                                    KODI_HANDLE addonInstance)
{
  const auto& it = m_usedInstances.find(instanceID);
  if (it != m_usedInstances.end())
  {
    it->second->Disconnect();
    m_usedInstances.erase(it);
  }
  g_pvrclient = nullptr;
}

//-- SetSetting ---------------------------------------------------------------
// Called everytime a setting is changed by the user and to inform AddOn about
// new setting and to do required stuff to apply it.
//-----------------------------------------------------------------------------
ADDON_STATUS CNextPVRAddon::SetSetting(const std::string& settingName, const kodi::CSettingValue& settingValue)
{
  std::string str = settingName;

  ADDON_STATUS status = settings.SetValue(settingName, settingValue);
  if (status == ADDON_STATUS_NEED_SETTINGS)
  {
    status = ADDON_STATUS_OK;
    // need to trigger recording update;
    g_pvrclient->ForceRecordingUpdate();
  }
  return status;
}
ADDONCREATOR(CNextPVRAddon);
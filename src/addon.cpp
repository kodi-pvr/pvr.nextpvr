/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "addon.h"
#include "pvrclient-nextpvr.h"
#include "AddonSettings.h"
#include "utilities/SettingsMigration.h"
/* User adjustable settings are saved here.
 * Default values are defined inside addon.h
 * and exported to the other source files.
 */

/***********************************************************
 * Standard AddOn related public library functions
 ***********************************************************/

//-- Create -------------------------------------------------------------------
// Called after loading of the dll, all steps to become Client functional
// must be performed here.
//-----------------------------------------------------------------------------

using namespace NextPVR;
using namespace NextPVR::utilities;

ADDON_STATUS CNextPVRAddon::Create()
{
  m_settings.reset(new AddonSettings());
  kodi::Log(ADDON_LOG_INFO, "Creating NextPVR PVR-Client");
  return ADDON_STATUS_OK;
}

ADDON_STATUS CNextPVRAddon::CreateInstance(const kodi::addon::IInstanceInfo& instance,
                                           KODI_ADDON_INSTANCE_HDL& hdl)
{

  /* Create connection to NextPVR KODI TV client */
  cPVRClientNextPVR* client = new cPVRClientNextPVR(*this, instance);

  if (SettingsMigration::MigrateSettings(*client))
  {
    // Initial client operated on old/incomplete settings
    delete client;
    client = new cPVRClientNextPVR(*this, instance);
  }

  ADDON_STATUS status = client->Connect();

  if (status != ADDON_STATUS_PERMANENT_FAILURE)
  {
    status = ADDON_STATUS_OK;
    hdl = client;
    m_usedInstances.emplace(std::make_pair(instance.GetID(), client));
    //client->m_menuhook.ConfigureMenuHook();
  }

  return status;
}

//-- Destroy ------------------------------------------------------------------
// Used during destruction of the client, all steps to do clean and safe Create
// again must be done.
//-----------------------------------------------------------------------------
void CNextPVRAddon::DestroyInstance(const kodi::addon::IInstanceInfo& instance,
                                    const KODI_ADDON_INSTANCE_HDL hdl)
{
  const auto& it = m_usedInstances.find(instance.GetID());
  if (it != m_usedInstances.end())
  {
    it->second->Disconnect();
    m_usedInstances.erase(it);
  }
}

ADDON_STATUS CNextPVRAddon::SetSetting(const std::string& settingName,
                                   const kodi::addon::CSettingValue& settingValue)
{
  return m_settings->SetSetting(settingName, settingValue);
}


ADDONCREATOR(CNextPVRAddon);
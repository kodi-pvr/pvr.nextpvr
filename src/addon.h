/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#ifndef ADDON_H
#define ADDON_H

#pragma once

#include "Settings.h"

#include <kodi/AddonBase.h>
#include <unordered_map>


class cPVRClientNextPVR;
extern cPVRClientNextPVR* g_pvrclient;

class ATTRIBUTE_HIDDEN CNextPVRAddon : public kodi::addon::CAddonBase
{
public:
  CNextPVRAddon() = default;

  ADDON_STATUS Create() override;
  ADDON_STATUS GetStatus() override { return m_curStatus; }
  ADDON_STATUS SetSetting(const std::string& settingName,
    const kodi::CSettingValue& settingValue) override;
  ADDON_STATUS CreateInstance(int instanceType, const std::string& instanceID,
    KODI_HANDLE instance, const std::string& version,
    KODI_HANDLE& addonInstance) override;
  void DestroyInstance(int instanceType,
    const std::string& instanceID,
    KODI_HANDLE addonInstance) override;

private:
  ADDON_STATUS m_curStatus = ADDON_STATUS_UNKNOWN;
  std::unordered_map<std::string, cPVRClientNextPVR*> m_usedInstances;
};

typedef unsigned char byte;

std::string UriEncode(const std::string sSrc);

#endif /* ADDON_H */

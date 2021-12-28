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

class ATTR_DLL_LOCAL CNextPVRAddon : public kodi::addon::CAddonBase
{
public:
  CNextPVRAddon() = default;

  ADDON_STATUS Create() override;
  ADDON_STATUS SetSetting(const std::string& settingName,
    const kodi::addon::CSettingValue& settingValue) override;
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
    KODI_ADDON_INSTANCE_HDL& hdl) override;
  void DestroyInstance(const kodi::addon::IInstanceInfo& instance,
    const KODI_ADDON_INSTANCE_HDL hdl) override;

private:
  std::unordered_map<std::string, cPVRClientNextPVR*> m_usedInstances;
};

typedef unsigned char byte;

std::string UriEncode(const std::string sSrc);

#endif /* ADDON_H */

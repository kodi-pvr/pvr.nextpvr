/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "AddonSettings.h"

#include "utilities/SettingsMigration.h"

#include "kodi/General.h"

using namespace NextPVR;
using namespace NextPVR::utilities;

namespace
{

bool ReadBoolSetting(const std::string& key, bool def)
{
  bool value;
  if (kodi::addon::CheckSettingBoolean(key, value))
    return value;

  return def;
}

} // unnamed namespace

AddonSettings::AddonSettings()
{
  ReadSettings();
}

void AddonSettings::ReadSettings()
{

}

ADDON_STATUS AddonSettings::SetSetting(const std::string& key,
                                       const kodi::addon::CSettingValue& value)
{
  if (SettingsMigration::IsMigrationSetting(key))
  {
    // ignore settings from pre-multi-instance setup
    return ADDON_STATUS_OK;
  }

  kodi::Log(ADDON_LOG_ERROR, "AddonSettings::SetSetting - unknown setting '%s'",
              key.c_str());
  return ADDON_STATUS_UNKNOWN;
}

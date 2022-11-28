/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <string>
#include "tinyxml2.h"
namespace kodi
{
namespace addon
{
class IAddonInstance;
}
} // namespace kodi

namespace NextPVR
{
namespace utilities
{
class SettingsMigration
{
public:
  static bool MigrateSettings(kodi::addon::IAddonInstance& target);
  static bool IsMigrationSetting(const std::string& key);

private:
  SettingsMigration() = delete;
  explicit SettingsMigration(kodi::addon::IAddonInstance& target) : m_target(target) {}

  void MigrateStringSetting(const char* key, const std::string& defaultValue, tinyxml2::XMLNode* rootNode);
  void MigrateIntSetting(const char* key, int defaultValue, tinyxml2::XMLNode* rootNode);
  void MigrateBoolSetting(const char* key, bool defaultValue, tinyxml2::XMLNode* rootNode);
  void MoveResourceFiles();

  bool Changed() const { return m_changed; }
  kodi::addon::IAddonInstance& m_target;
  bool m_changed{false};
};

} // namespace utilities
} // namespace NextPVR

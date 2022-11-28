/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// #include "../pvrclient-nextpvr.h"

#include "SettingsMigration.h"

#include "kodi/General.h"
#include "kodi/Filesystem.h"

#include "utilities/XMLUtils.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace NextPVR;
using namespace NextPVR::utilities;

namespace
{
// <setting name, default value> maps
const std::vector<std::pair<const char*, const char*>> stringMap = {
    {"host", "127.0.0.1"}, {"pin", "0000"}, {"hostprotocol", "http"}, {"host_mac", "00:00:00:00:00:00"},
    {"resolution", "720"}, {"diskspace", "Default"}};

const std::vector<std::pair<const char*, int>> intMap = {{"port", 8866},
                                                         {"livestreamingmethod5", 2},
                                                         {"prebuffer5", 1},
                                                         {"woltimeout", 20},
                                                         {"chunklivetv", 64},
                                                         {"chunkrecording", 32}};

const std::vector<std::pair<const char*, bool>> boolMap = {{"wolenable", false},
                                                           {"uselivestreams", false},
                                                           {"ffmpegdirect", false},
                                                           {"showradio", true},
                                                           {"remoteaccess", false},
                                                           {"guideartwork", false},
                                                           {"guideartworkportrait", false},
                                                           {"castcrew", false},
                                                           {"flattenrecording", false},
                                                           {"showroot", false},
                                                           {"separateseasons", false},
                                                           {"genrestring", false},
                                                           {"ignorepadding", true},
                                                           {"backendresume", true}};

} // unnamed namespace

bool SettingsMigration::MigrateSettings(kodi::addon::IAddonInstance& target)
{
  std::string stringValue;
  bool boolValue{false};
  int intValue{0};

  if (target.CheckInstanceSettingString("kodi_addon_instance_name", stringValue) &&
      !stringValue.empty())
  {
    // Instance already has valid instance settings
    return false;
  }
    // ask XBMC to read settings for us
  tinyxml2::XMLDocument m_doc;

  if (m_doc.Parse(kodi::vfs::TranslateSpecialProtocol("special://xbmc/addons/pvr.nextpvr/resources/settings.xml").c_str()) == tinyxml2::XML_SUCCESS)
  {


    // Read pre-multi-instance settings from settings.xml, transfer to instance settings
    SettingsMigration mig(target);

    mig.MoveResourceFiles();

    for (const auto& setting : stringMap)
      mig.MigrateStringSetting(setting.first, setting.second);

    for (const auto& setting : intMap)
      mig.MigrateIntSetting(setting.first, setting.second);

    for (const auto& setting : boolMap)
      mig.MigrateBoolSetting(setting.first, setting.second);

    if (mig.Changed())
    {
      // Set a title for the new instance settings
      std::string title;
      target.CheckInstanceSettingString("host", title);
      if (title.empty())
        title = "Migrated Add-on Config";

      target.SetInstanceSettingString("kodi_addon_instance_name", title);
      return true;
    }
  }
  return false;
}

void SettingsMigration::MoveResourceFiles()
{
  std::string marti = kodi::vfs::TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/");
  std::vector<kodi::vfs::CDirEntry> icons;
  if (kodi::vfs::GetDirectory("special://profile/addon_data/pvr.nextpvr/", "nextpvr-ch*.png", icons))
  {
    kodi::Log(ADDON_LOG_DEBUG, "Moving %d channel icons", icons.size());
    for (auto const& it : icons)
    {
      if (!it.IsFolder())
      {
        const std::string moveme = it.Path();

        kodi::Log(ADDON_LOG_DEBUG, "Move %s rc:%d", kodi::vfs::TranslateSpecialProtocol(moveme).c_str(),
          kodi::vfs::RenameFile(moveme, "special://profile/addon_data/pvr.nextpvr/1/" + it.Label()));
      }
    }
  }
  kodi::vfs::DeleteFile("special://profile/addon_data/pvr.nextpvr/connection.flag");
  kodi::vfs::DeleteFile("special://profile/addon_data/pvr.nextpvr/LiveStreams.xml");
}

bool SettingsMigration::IsMigrationSetting(const std::string& key)
{
  return std::any_of(stringMap.cbegin(), stringMap.cend(),
                     [&key](const auto& entry) { return entry.first == key; }) ||
         std::any_of(intMap.cbegin(), intMap.cend(),
                     [&key](const auto& entry) { return entry.first == key; }) ||
         std::any_of(boolMap.cbegin(), boolMap.cend(),
                     [&key](const auto& entry) { return entry.first == key; });
}

void SettingsMigration::MigrateStringSetting(const char* key, const std::string& defaultValue)
{
  std::string value;
  if (kodi::addon::CheckSettingString(key, value) && value != defaultValue)
  {
    m_target.SetInstanceSettingString(key, value);
    m_changed = true;
  }
}

void SettingsMigration::MigrateIntSetting(const char* key, int defaultValue)
{
  int value;
  if (kodi::addon::CheckSettingInt(key, value) && value != defaultValue)
  {
    m_target.SetInstanceSettingInt(key, value);
    m_changed = true;
  }
}

void SettingsMigration::MigrateBoolSetting(const char* key, bool defaultValue)
{
  bool value;
  if (kodi::addon::CheckSettingBoolean(key, value) && value != defaultValue)
  {
    m_target.SetInstanceSettingBoolean(key, value);
    m_changed = true;
  }
}

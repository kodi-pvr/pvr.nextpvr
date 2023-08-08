/*
 *  Copyright (C) 2005-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

// #include "../pvrclient-nextpvr.h"

#include "SettingsMigration.h"

#include "kodi/General.h"
#include "kodi/Filesystem.h"
#include <sys/stat.h>

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

  #if defined(TARGET_DARWIN_EMBEDDED)
  struct stat sb;
  kodi::vfs::CFile setting;
  std::string instanceFile = kodi::tools::StringUtils::Format("special://profile/addon_data/pvr.nextpvr/instance-settings-1.xml");
  if (setting.OpenFile(instanceFile, ADDON_READ_NO_CACHE))
  {
    kodi::Log(ADDON_LOG_INFO, "Instance vfs xml opened for read");
  }
  else
  {
    kodi::Log(ADDON_LOG_INFO, "Instance vfs xml did not open");
  }
  setting.Close();
  bool instanceExists = kodi::vfs::FileExists(instanceFile);
  std::string original = kodi::vfs::TranslateSpecialProtocol(instanceFile);
  kodi::Log(ADDON_LOG_INFO, "Instance xml check %d %s", instanceExists, instanceFile.c_str());
  kodi::Log(ADDON_LOG_INFO, "Instance xml check stat  %d %s %d", stat(original.c_str(), &sb), original.c_str(), errno);
  std::string response;
  if (setting.OpenFile(original, ADDON_READ_NO_CACHE))
  {
    kodi::Log(ADDON_LOG_INFO, "Instance xml opened %d %s", setting.GetLength(), original.c_str());
    char buffer[1025] = { 0 };
    int count;
    while ((count = setting.Read(buffer, 1024)))
    {
      response.append(buffer, count);
    }
    kodi::Log(ADDON_LOG_INFO, "Instance xml read %s", response.c_str());
  }
  else
  {
    kodi::Log(ADDON_LOG_INFO, "Instance xml read error %d", errno);
  }
  setting.Close();
  #endif
  target.CheckInstanceSettingString("kodi_addon_instance_name", stringValue);
  if (!stringValue.empty())
  {
    // Instance already has valid instance settings
    kodi::Log(ADDON_LOG_INFO, "Using saved instance file %s", stringValue.c_str());
    return false;
  }

  #if defined(TARGET_DARWIN_EMBEDDED)
  kodi::Log(ADDON_LOG_INFO, "Empty instance name tvOS %d %s", target.IsInstanceSettingUsingDefault("kodi_addon_instance_name"), stringValue.c_str());
  std::string title;
  target.CheckInstanceSettingString("host", title);
  kodi::Log(ADDON_LOG_INFO, "Use tvOS hostname  %d %s", target.IsInstanceSettingUsingDefault("host"), title.c_str());
  target.SetInstanceSettingString("kodi_addon_instance_name", title);
  instanceFile = kodi::tools::StringUtils::Format("special://profile/addon_data/pvr.nextpvr/1/");
  instanceExists = kodi::vfs::DirectoryExists(instanceFile);
  original = kodi::vfs::TranslateSpecialProtocol(instanceFile);
  kodi::Log(ADDON_LOG_INFO, "Instance folder check %d %s", instanceExists, instanceFile.c_str());
  kodi::Log(ADDON_LOG_INFO, "Instance folder check stat %d %s %d", stat(original.c_str(), &sb), original.c_str(), errno);
  if (instanceExists || stat(original.c_str(), &sb) == 0)
  {
    return false;
  }
  #endif

    // ask XBMC to read settings for us
  tinyxml2::XMLDocument doc;

  if (doc.LoadFile(kodi::vfs::TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/settings.xml").c_str()) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* rootNode = doc.FirstChild();
    if (rootNode == nullptr)
      return false;

    // Read pre-multi-instance settings from settings.xml, transfer to instance settings
    SettingsMigration mig(target);

    mig.MoveResourceFiles();

    for (const auto& setting : stringMap)
      mig.MigrateStringSetting(setting.first, setting.second, rootNode);

    for (const auto& setting : intMap)
      mig.MigrateIntSetting(setting.first, setting.second, rootNode);

    for (const auto& setting : boolMap)
      mig.MigrateBoolSetting(setting.first, setting.second, rootNode);

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
  std::string original = kodi::vfs::TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/");

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

void SettingsMigration::MigrateStringSetting(const char* key, const std::string& defaultValue, tinyxml2::XMLNode* rootNode)
{
  std::string value;
  tinyxml2::XMLElement* child = rootNode->FirstChildElement("setting");
  while (child != nullptr)
  {
    if (child->Attribute("id", key))
    {
      value = child->GetText();
      if (value != defaultValue)
      {
        m_target.SetInstanceSettingString(key, value);
        m_changed = true;
      }
      break;
    }
    child = child->NextSiblingElement();
  }
}

void SettingsMigration::MigrateIntSetting(const char* key, int defaultValue, tinyxml2::XMLNode* rootNode)
{
  int value = defaultValue;
  tinyxml2::XMLElement* child = rootNode->FirstChildElement("setting");
  while (child != nullptr)
  {
    if (child->Attribute("id", key))
    {
      child->QueryIntText(&value);
      if (value != defaultValue)
      {
        m_target.SetInstanceSettingInt(key, value);
        m_changed = true;
      }
      break;
    }
    child = child->NextSiblingElement();
  }

}

void SettingsMigration::MigrateBoolSetting(const char* key, bool defaultValue, tinyxml2::XMLNode* rootNode)
{
  bool value = defaultValue;
  tinyxml2::XMLElement* child = rootNode->FirstChildElement("setting");
  while (child != nullptr)
  {
    if (child->Attribute("id", key) != nullptr)
    {
      child->QueryBoolText(&value);
      if (value != defaultValue)
      {
        m_target.SetInstanceSettingBoolean(key, value);
        m_changed = true;
      }
      break;
    }
    child = child->NextSiblingElement();
  }
}

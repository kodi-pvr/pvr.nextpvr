/*
 *  Copyright (C) 2005-2024 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/addon-instance/PVR.h>
#include <kodi/General.h>
#include "../InstanceSettings.h"
#include <map>
#include <string>


namespace NextPVR
{
  struct GenreBlock { std::string description; int genreType; int genreSubType; };
  static const std::string GENRE_KODI_DVB_FILEPATH = "special://home/addons/pvr.nextpvr/resources/genre-mapping.xml";
  class ATTR_DLL_LOCAL GenreMapper
  {

  public:
    GenreMapper(const std::shared_ptr<InstanceSettings>& settings);
    ~GenreMapper();

    int GetGenreType(std::string code);
    int GetGenreSubType(std::string code);
    bool ParseAllGenres(const tinyxml2::XMLNode* node, GenreBlock& genreBlock);
    bool UseDvbGenre();

  private:
    GenreMapper() = default;
    GenreMapper(GenreMapper const&) = delete;
    void operator=(GenreMapper const&) = delete;

    int GetGenreTypeFromCombined(int combinedGenreType);
    int GetGenreSubTypeFromCombined(int combinedGenreType);
    int LookupGenreValueInMaps(const std::string& genreText);

    void LoadGenreTextMappingFiles();
    bool LoadTextToIdGenreFile(const std::string& xmlFile, std::map<std::string, int>& map);
    std::map<std::string, int> m_genreMap;
    const std::shared_ptr<InstanceSettings> m_settings;
  };
}  // namespace NextPVR

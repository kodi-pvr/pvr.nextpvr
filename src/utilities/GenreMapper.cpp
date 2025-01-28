/*
 *  Copyright (C) 2005-2024 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#include <kodi/Filesystem.h>
#include "GenreMapper.h"
#include "XMLUtils.h"
#include "tinyxml2.h"

#include <cstdlib>

using namespace NextPVR;
using namespace NextPVR::utilities;

GenreMapper::GenreMapper(const std::shared_ptr<InstanceSettings>& settings) : m_settings(settings)
{
  LoadGenreTextMappingFiles();
}

GenreMapper::~GenreMapper() {}


bool GenreMapper::UseDvbGenre()
{
  return !m_settings->m_genreString;
}

int GenreMapper::GetGenreTypeFromCombined(int combinedGenreType)
{
  return combinedGenreType & 0xF0;
}

int GenreMapper::GetGenreSubTypeFromCombined(int combinedGenreType)
{
  return combinedGenreType & 0x0F;
}


int GenreMapper::LookupGenreValueInMaps(const std::string& genreText)
{
  int genreType = EPG_EVENT_CONTENTMASK_UNDEFINED;

  auto genreMapSearch = m_genreMap.find(genreText);
  if (genreMapSearch != m_genreMap.end())
  {
    genreType = genreMapSearch->second;
  }
  return genreType;
}

void GenreMapper::LoadGenreTextMappingFiles()
{
  if (!LoadTextToIdGenreFile(GENRE_KODI_DVB_FILEPATH, m_genreMap))
    kodi::Log(ADDON_LOG_ERROR, "%s Could not load text to genre id file: %s", __func__, GENRE_KODI_DVB_FILEPATH.c_str());

}

bool GenreMapper::ParseAllGenres(const tinyxml2::XMLNode* node, GenreBlock& genreBlock)
{
  std::string allGenres;
  if (XMLUtils::GetAdditiveString(node->FirstChildElement("genres"), "genre", EPG_STRING_TOKEN_SEPARATOR, allGenres, true))
  {
    if (allGenres.find(EPG_STRING_TOKEN_SEPARATOR) != std::string::npos)
    {
      if (UseDvbGenre())
      {
        std::vector<std::string> genreCodes = kodi::tools::StringUtils::Split(allGenres, EPG_STRING_TOKEN_SEPARATOR);
        if (genreCodes.size() == 2)
        {
          if (genreBlock.genreType == EPG_EVENT_CONTENTMASK_UNDEFINED)
            genreBlock.genreType = GetGenreType(genreCodes[0]);

          if (genreCodes[0] == "Show / Game show")
            genreBlock.genreType = 48;

          if (genreBlock.genreType == GetGenreType(genreCodes[0]))
          {
            if (genreBlock.genreType == GetGenreType(genreCodes[1]))
              genreBlock.genreSubType = GetGenreSubType(genreCodes[1]);
          }
        }
      }
      if (genreBlock.genreSubType == EPG_EVENT_CONTENTMASK_UNDEFINED)
      {
        genreBlock.genreSubType = EPG_GENRE_USE_STRING;
        genreBlock.description = allGenres;
      }
    }
    else if (!UseDvbGenre() && genreBlock.genreSubType != EPG_EVENT_CONTENTMASK_UNDEFINED)
    {
      genreBlock.description = allGenres;
      genreBlock.genreSubType = EPG_GENRE_USE_STRING;
    }

    return true;
  }
  return false;
}

bool GenreMapper::LoadTextToIdGenreFile(const std::string& xmlFile, std::map<std::string, int>& map)
{
  map.clear();

  if (!kodi::vfs::FileExists(xmlFile.c_str()))
  {
    kodi::Log(ADDON_LOG_ERROR, "%s No XML file found: %s", __func__, xmlFile.c_str());
    return false;
  }

  kodi::Log(ADDON_LOG_DEBUG, "%s Loading XML File: %s", __func__, xmlFile.c_str());

  std::string fileContents;
  kodi::vfs::CFile loadXml;
  if (loadXml.OpenFile(xmlFile, ADDON_READ_NO_CACHE))
  {
    char buffer[1025] = { 0 };
    int count;
    while ((count = loadXml.Read(buffer, 1024)))
    {
      fileContents.append(buffer, count);
    }
  }
  loadXml.Close();

  tinyxml2::XMLDocument xmlDoc;

  if (xmlDoc.Parse(fileContents.c_str()) != tinyxml2::XML_SUCCESS)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s Unable to parse XML: %s at line %d", __func__, xmlDoc.ErrorStr(), xmlDoc.ErrorLineNum());
    return false;
  }

  tinyxml2::XMLHandle hDoc(&xmlDoc);

  tinyxml2::XMLElement* pNode = hDoc.FirstChildElement("translations").ToElement();

  if (!pNode)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s Could not find <translations> element", __func__);
    return false;
  }

  pNode = pNode->FirstChildElement("genre");

  if (!pNode)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s Could not find <genre> element", __func__);
    return false;
  }

  for (; pNode != nullptr; pNode = pNode->NextSiblingElement("genre"))
  {
    std::string textMapping;

    textMapping = pNode->Attribute("name");
    int type = atoi(pNode->Attribute("type"));
    int subtype = atoi(pNode->Attribute("subtype"));
    if (!textMapping.empty())
    {
      map.insert({ textMapping, type | subtype });
      kodi::Log(ADDON_LOG_DEBUG, "%s Read Text Mapping text=%s, targetId=%#02X", __func__, textMapping.c_str(), type|subtype);
    }
  }
  return true;
}

int GenreMapper::GetGenreType(std::string code)
{
  return GetGenreTypeFromCombined(LookupGenreValueInMaps(code));
};

int GenreMapper::GetGenreSubType(std::string code)
{
  return GetGenreSubTypeFromCombined(LookupGenreValueInMaps(code));
};

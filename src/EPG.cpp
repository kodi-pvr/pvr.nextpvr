/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "EPG.h"

#include "pvrclient-nextpvr.h"
#include "utilities/XMLUtils.h"

#include <kodi/tools/StringUtils.h>
#include <regex>

using namespace NextPVR;
using namespace NextPVR::utilities;

/************************************************************/
/** EPG handling */

EPG::EPG(const std::shared_ptr<InstanceSettings>& settings, Request& request, Recordings& recordings, Channels& channels) :
  m_settings(settings),
  m_request(request),
  m_recordings(recordings),
  m_channels(channels)
{
}

PVR_ERROR EPG::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  std::pair<bool, bool> channelDetail;
  channelDetail = m_channels.m_channelDetails[channelUid];
  if (channelDetail.first == true)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Skipping %d", channelUid);
    return PVR_ERROR_NO_ERROR;
  }

  if (end < (time(nullptr) - 24 * 3600))
  {
    kodi::Log(ADDON_LOG_DEBUG, "Skipping expired EPG data %d %ld %lld", channelUid, start, end);
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  std::string request = kodi::tools::StringUtils::Format("channel.listings&channel_id=%d&start=%d&end=%d&genre=all", channelUid, static_cast<int>(start), static_cast<int>(end));
  if (m_settings->m_castcrew)
    request.append("&extras=true");

  tinyxml2::XMLDocument doc;

  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* listingsNode = doc.RootElement()->FirstChildElement("listings");
    for (tinyxml2::XMLNode* pListingNode = listingsNode->FirstChildElement("l"); pListingNode; pListingNode = pListingNode->NextSiblingElement())
    {
      kodi::addon::PVREPGTag broadcast;
      std::string title;
      std::string description;
      std::string subtitle;
      XMLUtils::GetString(pListingNode, "name", title);
      XMLUtils::GetString(pListingNode, "description", description);

      if (XMLUtils::GetString(pListingNode, "subtitle", subtitle))
      {
        if (description != subtitle + ":" && kodi::tools::StringUtils::StartsWith(description, subtitle + ": "))
        {
          description = description.substr(subtitle.length() + 2);
        }
      }

      std::string startTime;
      std::string endTime;
      XMLUtils::GetString(pListingNode, "start", startTime);
      startTime.resize(10);
      XMLUtils::GetString(pListingNode, "end", endTime);
      endTime.resize(10);

      const std::string oidLookup(endTime + ":" + std::to_string(channelUid));

      broadcast.SetTitle(title);
      broadcast.SetUniqueChannelId(channelUid);
      broadcast.SetStartTime(stol(startTime));
      broadcast.SetUniqueBroadcastId(stoi(endTime));
      broadcast.SetEndTime(stol(endTime));
      broadcast.SetPlot(description);

      std::string artworkPath;
      if (m_settings->m_downloadGuideArtwork)
      {
        if (m_settings->m_sendSidWithMetadata)
          artworkPath = kodi::tools::StringUtils::Format("%s/service?method=channel.show.artwork&sid=%s&name=%s", m_settings->m_urlBase, m_request.GetSID(), UriEncode(title).c_str());
        else
          artworkPath = kodi::tools::StringUtils::Format("%s/service?method=channel.show.artwork&name=%s", m_settings->m_urlBase, UriEncode(title).c_str());

        if (m_settings->m_guideArtPortrait)
          artworkPath += "&prefer=poster";
        else
          artworkPath += "&prefer=landscape";
        broadcast.SetIconPath(artworkPath);
      }
      std::string sGenre;
      if (XMLUtils::GetString(pListingNode, "genre", sGenre))
      {
        broadcast.SetGenreDescription(sGenre);
        broadcast.SetGenreType(EPG_GENRE_USE_STRING);
      }
      else
      {
        // genre type
        broadcast.SetGenreType(XMLUtils::GetIntValue(pListingNode, "genre_type"));
        broadcast.SetGenreSubType(XMLUtils::GetIntValue(pListingNode, "genre_sub_type"));

      }
      std::string allGenres;
      if (XMLUtils::GetAdditiveString(pListingNode->FirstChildElement("genres"), "genre", EPG_STRING_TOKEN_SEPARATOR, allGenres, true))
      {
        if (allGenres.find(EPG_STRING_TOKEN_SEPARATOR) != std::string::npos)
        {
          if (broadcast.GetGenreType() != EPG_GENRE_USE_STRING)
          {
            broadcast.SetGenreSubType(EPG_GENRE_USE_STRING);
          }
          broadcast.SetGenreDescription(allGenres);
        }
        else if (m_settings->m_genreString && broadcast.GetGenreSubType() != EPG_GENRE_USE_STRING)
        {
          broadcast.SetGenreDescription(allGenres);
          broadcast.SetGenreSubType(EPG_GENRE_USE_STRING);
        }

      }

      int season{EPG_TAG_INVALID_SERIES_EPISODE};
      int episode{EPG_TAG_INVALID_SERIES_EPISODE};
      XMLUtils::GetInt(pListingNode, "season", season);
      XMLUtils::GetInt(pListingNode, "episode", episode);
      broadcast.SetEpisodeNumber(episode);
      broadcast.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);
      // Backend could send episode only as S00 and parts are not supported
      if (season <= 0 || episode == EPG_TAG_INVALID_SERIES_EPISODE)
      {
        static std::regex base_regex("^.*\\([eE][pP](\\d+)(?:/?(\\d+))?\\)");
        std::smatch base_match;
        if (std::regex_search(description, base_match, base_regex))
        {
          broadcast.SetEpisodeNumber(std::atoi(base_match[1].str().c_str()));
          if (base_match[2].matched)
            broadcast.SetEpisodePartNumber(std::atoi(base_match[2].str().c_str()));
        }
        else if (std::regex_search(description, base_match, std::regex("^([1-9]\\d*)/([1-9]\\d*)\\.")))
        {
          broadcast.SetEpisodeNumber(std::atoi(base_match[1].str().c_str()));
          broadcast.SetEpisodePartNumber(std::atoi(base_match[2].str().c_str()));
        }
      }
      if (season != EPG_TAG_INVALID_SERIES_EPISODE)
      {
        // clear out NextPVR formatted data, Kodi supports S/E display
        if (subtitle == kodi::tools::StringUtils::Format("S%02dE%02d", season, episode))
        {
          subtitle.clear();
        }
        if (season == 0)
          season = EPG_TAG_INVALID_SERIES_EPISODE;
      }
      broadcast.SetSeriesNumber(season);
      broadcast.SetEpisodeName(subtitle);

      int year{YEAR_NOT_SET};
      if (XMLUtils::GetInt(pListingNode, "year", year))
      {
        broadcast.SetYear(year);
      }

      std::string original;
      if (XMLUtils::GetString(pListingNode, "original", original))
      {
        // For movies with YYYY-MM-DD use only YYYY
        if (broadcast.GetGenreType() == EPG_EVENT_CONTENTMASK_MOVIEDRAMA && broadcast.GetGenreSubType() == EPG_EVENT_CONTENTSUBMASK_MOVIEDRAMA_GENERAL
          && year == YEAR_NOT_SET && original.length() > 4)
        {
          const std::string originalYear = kodi::tools::StringUtils::Mid(original, 0, 4);
          year = std::atoi(originalYear.c_str());
          if (year != 0)
            broadcast.SetYear(year);
        }
        else
        {
          broadcast.SetFirstAired(original);
        }
      }


      bool firstrun;
      if (XMLUtils::GetBoolean(pListingNode, "firstrun", firstrun))
      {
        if (firstrun)
        {
          std::string significance;
          XMLUtils::GetString(pListingNode, "significance", significance);
          if (significance == "Live")
          {
            broadcast.SetFlags(EPG_TAG_FLAG_IS_LIVE);
          }
          else if (significance.find("Premiere") != std::string::npos)
          {
            broadcast.SetFlags(EPG_TAG_FLAG_IS_PREMIERE);
          }
          else if (significance.find("Finale") != std::string::npos)
          {
            broadcast.SetFlags(EPG_TAG_FLAG_IS_FINALE);
          }
          else if (m_settings->m_showNew)
          {
            broadcast.SetFlags(EPG_TAG_FLAG_IS_NEW);
          }
        }
      }
      if (m_settings->m_castcrew)
      {
        std::string castcrew;
        XMLUtils::GetString(pListingNode, "cast", castcrew);
        std::replace(castcrew.begin(), castcrew.end(), ';', ',');
        kodi::tools::StringUtils::Replace(castcrew, "Actor:", "");
        kodi::tools::StringUtils::Replace(castcrew, "Host:", "");
        broadcast.SetCast(castcrew);

        castcrew.clear();
        XMLUtils::GetString(pListingNode, "crew", castcrew);
        std::vector<std::string> allcrew = kodi::tools::StringUtils::Split(castcrew, ";", 0);
        std::string writer;
        std::string director;
        for (auto it = allcrew.begin(); it != allcrew.end(); ++it)
        {
          std::vector<std::string> onecrew = kodi::tools::StringUtils::Split(*it, ":", 0);
          if (onecrew.size() == 2)
          {
            if (kodi::tools::StringUtils::ContainsKeyword(onecrew[0].c_str(), { "Writer", "Screenwriter" }))
            {
              if (!writer.empty())
                writer.append(EPG_STRING_TOKEN_SEPARATOR);
              writer.append(onecrew[1]);
            }
            if (onecrew[0] == "Director")
            {
              if (!director.empty())
                director.append(EPG_STRING_TOKEN_SEPARATOR);
              director.append(onecrew[1]);
            }
          }
        }
        broadcast.SetDirector(director);
        broadcast.SetWriter(writer);
      }
      std::string rating;
      if (XMLUtils::GetString(pListingNode, "star_rating", rating))
      {
        std::regex base_regex("(\\d+[.]?\\d*)(?:(?:/)(\\d+[.]?\\d*))?");
        std::smatch base_match;
        if (std::regex_match(rating, base_match, base_regex))
        {
          if (base_match.size() == 3)
          {
            double quotient = std::atof(base_match[1].str().c_str());
            double denominator = std::atof(base_match[2].str().c_str());
            // if single value passed assume base 4
            if (denominator == 0)
              denominator = 4;
            int starRating = (quotient / denominator * 10.0) + 0.5;
            broadcast.SetStarRating(starRating);
          }
        }
      }
      results.Add(broadcast);
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

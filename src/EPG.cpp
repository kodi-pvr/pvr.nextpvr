/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "EPG.h"

#include "kodi/util/XMLUtils.h"
#include "pvrclient-nextpvr.h"

#include <p8-platform/util/StringUtils.h>

using namespace NextPVR;

/************************************************************/
/** EPG handling */

PVR_ERROR EPG::GetEPGForChannel(int channelUid, time_t start, time_t end, kodi::addon::PVREPGTagsResultSet& results)
{
  std::string response;
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
  const std::string request = StringUtils::Format("/service?method=channel.listings&channel_id=%d&start=%d&end=%d&genre=all", channelUid, static_cast<int>(start), static_cast<int>(end));
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()))
    {
      TiXmlElement* listingsNode = doc.RootElement()->FirstChildElement("listings");
      for (TiXmlElement* pListingNode = listingsNode->FirstChildElement("l"); pListingNode; pListingNode = pListingNode->NextSiblingElement())
      {
        kodi::addon::PVREPGTag broadcast;
        std::string title;
        std::string description;
        std::string subtitle;
        XMLUtils::GetString(pListingNode, "name", title);
        XMLUtils::GetString(pListingNode, "description", description);

        if (XMLUtils::GetString(pListingNode, "subtitle", subtitle))
        {
          if (description != subtitle + ":" && StringUtils::StartsWith(description, subtitle + ": "))
          {
            description = description.substr(subtitle.length() + 2);
          }
        }

        broadcast.SetYear(g_pvrclient->XmlGetInt(pListingNode, "year"));

        std::string startTime;
        std::string endTime;
        XMLUtils::GetString(pListingNode, "start", startTime);
        startTime.resize(10);
        XMLUtils::GetString(pListingNode, "end", endTime);
        endTime.resize(10);

        const std::string oidLookup(endTime + ":" + std::to_string(channelUid));

        const int epgOid = g_pvrclient->XmlGetInt(pListingNode, "id");
        m_timers.m_epgOidLookup[oidLookup] = epgOid;

        broadcast.SetTitle(title);
        broadcast.SetEpisodeName(subtitle);
        broadcast.SetUniqueChannelId(channelUid);
        broadcast.SetStartTime(stol(startTime));
        broadcast.SetUniqueBroadcastId(stoi(endTime));
        broadcast.SetEndTime(stol(endTime));
        broadcast.SetPlot(description);

        std::string artworkPath;
        if (m_settings.m_downloadGuideArtwork)
        {
          // artwork URL
          if (m_settings.m_backendVersion < 50000)
            artworkPath = StringUtils::Format("http://%s:%d/service?method=channel.show.artwork&sid=%s&event_id=%d", m_settings.m_hostname.c_str(), m_settings.m_port, g_pvrclient->m_sid, epgOid);
          else
          {
            if (m_settings.m_sendSidWithMetadata)
              artworkPath = StringUtils::Format("http://%s:%d/service?method=channel.show.artwork&sid=%s&name=%s", m_settings.m_hostname.c_str(), m_settings.m_port, g_pvrclient->m_sid, UriEncode(title).c_str());
            else
              artworkPath = StringUtils::Format("http://%s:%d/service?method=channel.show.artwork&name=%s", m_settings.m_hostname.c_str(), m_settings.m_port, UriEncode(title).c_str());
            if (m_settings.m_guideArtPortrait)
              artworkPath += "&prefer=poster";
          }
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
          broadcast.SetGenreType(g_pvrclient->XmlGetInt(pListingNode, "genre_type"));
          broadcast.SetGenreSubType(g_pvrclient->XmlGetInt(pListingNode, "genre_sub_type"));

        }
        std::string allGenres;
        if (m_recordings.GetAdditiveString(pListingNode->FirstChildElement("genres"), "genre", EPG_STRING_TOKEN_SEPARATOR, allGenres, true))
        {
          if (allGenres.find(EPG_STRING_TOKEN_SEPARATOR) != std::string::npos)
          {
            if (broadcast.GetGenreType() != EPG_GENRE_USE_STRING)
            {
              broadcast.SetGenreSubType(EPG_GENRE_USE_STRING);
            }
            broadcast.SetGenreDescription(allGenres);
          }
        }
        broadcast.SetSeriesNumber(g_pvrclient->XmlGetInt(pListingNode, "season", EPG_TAG_INVALID_SERIES_EPISODE));
        broadcast.SetEpisodeNumber(g_pvrclient->XmlGetInt(pListingNode, "episode", EPG_TAG_INVALID_SERIES_EPISODE));
        broadcast.SetEpisodePartNumber(EPG_TAG_INVALID_SERIES_EPISODE);

        std::string original;
        XMLUtils::GetString(pListingNode, "original", original);
        broadcast.SetFirstAired(original);

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
            else if (m_settings.m_showNew)
            {
              broadcast.SetFlags(EPG_TAG_FLAG_IS_NEW);
            }
          }
        }
        results.Add(broadcast);
      }
    }
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_NO_ERROR;
}

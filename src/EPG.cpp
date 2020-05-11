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

#if defined(TARGET_WINDOWS)
#define atoll(S) _atoi64(S)
#else
#define MAXINT64 ULONG_MAX
#endif

using namespace NextPVR;

/************************************************************/
/** EPG handling */

PVR_ERROR EPG::GetEpg(ADDON_HANDLE handle, int iChannelUid, time_t iStart, time_t iEnd)
{
  EPG_TAG broadcast;

  std::string response;

  LOG_API_CALL(__FUNCTION__);

  std::pair<bool, bool> channelDetail;
  channelDetail = m_channelDetails[iChannelUid];
  if (channelDetail.first == true)
  {
    XBMC->Log(LOG_DEBUG, "Skipping %d", iChannelUid);
    return PVR_ERROR_NO_ERROR;
  }

  if (iEnd < (time(nullptr) - 24 * 3600))
  {
    XBMC->Log(LOG_DEBUG, "Skipping expired EPG data %d %ld %lld", iChannelUid, iStart, iEnd);
    return PVR_ERROR_INVALID_PARAMETERS;
  }
  const std::string request = StringUtils::Format("/service?method=channel.listings&channel_id=%d&start=%d&end=%d&genre=all", iChannelUid, static_cast<int>(iStart), static_cast<int>(iEnd));
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()))
    {
      TiXmlElement* listingsNode = doc.RootElement()->FirstChildElement("listings");
      for (TiXmlElement* pListingNode = listingsNode->FirstChildElement("l"); pListingNode; pListingNode = pListingNode->NextSiblingElement())
      {
        broadcast = {0};
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

        broadcast.iYear = g_pvrclient->XmlGetInt(pListingNode, "year");

        std::string startTime;
        std::string endTime;
        XMLUtils::GetString(pListingNode, "start", startTime);
        startTime.resize(10);
        XMLUtils::GetString(pListingNode, "end", endTime);
        endTime.resize(10);

        const std::string oidLookup(endTime + ":" + std::to_string(iChannelUid));

        const int epgOid = g_pvrclient->XmlGetInt(pListingNode, "id");
        m_timers.m_epgOidLookup[oidLookup] = epgOid;

        broadcast.strTitle = title.c_str();
        broadcast.strEpisodeName = subtitle.c_str();
        broadcast.iUniqueChannelId = iChannelUid;
        broadcast.startTime = stol(startTime);
        broadcast.iUniqueBroadcastId = stoi(endTime);
        broadcast.endTime = stol(endTime);
        broadcast.strPlot = description.c_str();

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
          broadcast.strIconPath = artworkPath.c_str();
        }
        std::string sGenre;
        if (XMLUtils::GetString(pListingNode, "genre", sGenre))
        {
          broadcast.strGenreDescription = sGenre.c_str();
          broadcast.iGenreType = EPG_GENRE_USE_STRING;
        }
        else
        {
          // genre type
          broadcast.iGenreType = g_pvrclient->XmlGetInt(pListingNode, "genre_type");
          broadcast.iGenreSubType = g_pvrclient->XmlGetInt(pListingNode, "genre_sub_type");

        }
        std::string allGenres;
        if (m_recordings.GetAdditiveString(pListingNode->FirstChildElement("genres"), "genre", EPG_STRING_TOKEN_SEPARATOR, allGenres, true))
        {
          if (allGenres.find(EPG_STRING_TOKEN_SEPARATOR) != std::string::npos)
          {
            if (broadcast.iGenreType != EPG_GENRE_USE_STRING)
            {
              broadcast.iGenreSubType = EPG_GENRE_USE_STRING;
            }
            broadcast.strGenreDescription = allGenres.c_str();
          }
        }
        broadcast.iSeriesNumber = g_pvrclient->XmlGetInt(pListingNode, "season", EPG_TAG_INVALID_SERIES_EPISODE);
        broadcast.iEpisodeNumber = g_pvrclient->XmlGetInt(pListingNode, "episode", EPG_TAG_INVALID_SERIES_EPISODE);
        broadcast.iEpisodePartNumber = EPG_TAG_INVALID_SERIES_EPISODE;

        std::string original;
        XMLUtils::GetString(pListingNode, "original", original);
        broadcast.strFirstAired = original.c_str();

        bool firstrun;
        if (XMLUtils::GetBoolean(pListingNode, "firstrun", firstrun))
        {
          if (firstrun)
          {
            std::string significance;
            XMLUtils::GetString(pListingNode, "significance", significance);
            if (significance == "Live")
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_LIVE;
            }
            else if (significance.find("Premiere") != std::string::npos)
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_PREMIERE;
            }
            else if (significance.find("Finale") != std::string::npos)
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_FINALE;
            }
            else if (m_settings.m_showNew)
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_NEW;
            }
          }
        }
        PVR->TransferEpgEntry(handle, &broadcast);
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Recordings.h"
#include "utilities/XMLUtils.h"

#include <kodi/General.h>
#include "pvrclient-nextpvr.h"

#include <regex>
#include <unordered_set>

#include <kodi/tools/StringUtils.h>

using namespace NextPVR;
using namespace NextPVR::utilities;

/************************************************************/
/** Record handling **/

PVR_ERROR Recordings::GetRecordingsAmount(bool deleted, int& amount)
{
  // need something more optimal, but this will do for now...
  // Return -1 on error.

  if (m_iRecordingCount != 0)
  {
    amount = m_iRecordingCount;
    return PVR_ERROR_NO_ERROR;
  }

  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("recording.list&filter=ready", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
    if (recordingsNode != nullptr)
    {
      tinyxml2::XMLNode* pRecordingNode;
      m_iRecordingCount = 0;
      for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        m_iRecordingCount++;
      }
    }
  }
  amount = m_iRecordingCount;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Recordings::GetDriveSpace(uint64_t& total, uint64_t& used)
{
  if (m_settings.m_diskSpace != "No" && m_checkedSpace < time(nullptr))
  {
    if (m_mutexSpace.try_lock())
    {
      tinyxml2::XMLDocument doc;
      // this call can take 3 seconds or longer.
      if (m_request.DoMethodRequest("system.space", doc) == tinyxml2::XML_SUCCESS)
      {
        m_checkedSpace = time(nullptr) + 10;
        std::string free;
        std::string total;
        std::unordered_set<std::string> drives;
        m_used = 0;
        m_total = 0;
        char* end;
        for (tinyxml2::XMLElement* directoryNode = doc.RootElement()->FirstChildElement("directory"); directoryNode; directoryNode = directoryNode->NextSiblingElement("directory"))
        {
          const std::string name = directoryNode->Attribute("name");
          if (m_settings.m_diskSpace == "Default")
          {
            if (name == "Default")
            {
              // ignore errno issues backend parses properly
              XMLUtils::GetString(directoryNode, "total", total);
              m_total = std::strtoull(total.c_str(), &end, 10) / 1024;
              XMLUtils::GetString(directoryNode, "free", free);
              m_used = m_total - std::strtoull(free.c_str(), &end, 10) / 1024;
              break;
            }
          }
          else //Span
          {
            // ignore errno issues backend parses properly
            XMLUtils::GetString(directoryNode, "total", total);
            XMLUtils::GetString(directoryNode, "free", free);
            // assume if free and total are the same it is the same drive
            std::string key = total + ":" + free;
            if (drives.find(key) == drives.end())
            {
              drives.insert(key);
              m_total += std::strtoull(total.c_str(), &end, 10) / 1024;
              m_used += std::strtoull(total.c_str(), &end, 10) / 1024 - std::strtoull(free.c_str(), &end, 10) / 1024;
            }
          }
        }
      }
      m_checkedSpace = time(nullptr) + 300;
      m_mutexSpace.unlock();
    }
  }
  total = m_total;
  used = m_used;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Recordings::GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results)
{
  // include already-completed recordings
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  m_hostFilenames.clear();
  m_lastPlayed.clear();
  m_playCount.clear();
  int recordingCount = 0;
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("recording.list&filter=all", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
    tinyxml2::XMLNode* pRecordingNode;
    std::map<std::string, int> names;
    std::map<std::string, int> seasons;
    if ((m_settings.m_flattenRecording && m_settings.m_kodiLook) || m_settings.m_separateSeasons)
    {
      kodi::addon::PVRRecording mytag;
      int season;
      for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        std::string status;
        XMLUtils::GetString(pRecordingNode, "status", status);
        if (status != "Ready" && status != "Recording")
          continue;
        std::string title;
        XMLUtils::GetString(pRecordingNode, "name", title);
        if ((m_settings.m_flattenRecording && m_settings.m_kodiLook))
          names[title]++;

        if (ParseNextPVRSubtitle(pRecordingNode, mytag))
          season = mytag.GetSeriesNumber();
        else
          season = PVR_RECORDING_INVALID_SERIES_EPISODE;

        if (seasons[title])
        {
          if (seasons[title] != std::numeric_limits<int>::max())
          {
            if (season != seasons[title])
              seasons[title] = std::numeric_limits<int>::max();
          }
        }
        else
        {
          seasons[title] = season;
        }
      }
    }
    for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
    {
      kodi::addon::PVRRecording tag;
      std::string title;
      XMLUtils::GetString(pRecordingNode, "name", title);
      if (UpdatePvrRecording(pRecordingNode, tag, title, names[title] == 1, seasons[title] == std::numeric_limits<int>::max()))
      {
        recordingCount++;
        results.Add(tag);
      }
    }
    m_iRecordingCount = recordingCount;
    // force read disk space
    m_checkedSpace = 0;
    uint64_t total;
    uint64_t used;
    GetDriveSpace(total, used);
    kodi::Log(ADDON_LOG_DEBUG, "Updated recordings %lld", g_pvrclient->m_lastRecordingUpdateTime);
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  g_pvrclient->m_lastRecordingUpdateTime = time(nullptr);
  return returnValue;
}

PVR_ERROR Recordings::GetRecordingsLastPlayedPosition()
{
  // include already-completed recordings
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("recording.list&filter=ready", doc) == tinyxml2::XML_SUCCESS)
  {
    m_lastPlayed.clear();
    for (const tinyxml2::XMLNode*  pRecordingNode = doc.RootElement()->FirstChildElement("recordings")->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      m_lastPlayed[XMLUtils::GetIntValue(pRecordingNode, "id")] =  XMLUtils::GetIntValue(pRecordingNode, "playback_position");
  }
  return returnValue;
}

bool Recordings::UpdatePvrRecording(const tinyxml2::XMLNode* pRecordingNode, kodi::addon::PVRRecording& tag, const std::string& title, bool flatten, bool multipleSeasons)
{
  std::string buffer;
  tag.SetTitle(title);

  XMLUtils::GetString(pRecordingNode, "start_time_ticks", buffer);
  tag.SetRecordingTime(stol(buffer));

  std::string status;
  XMLUtils::GetString(pRecordingNode, "status", status);
  if (status == "Pending" && tag.GetRecordingTime() > time(nullptr) + m_settings.m_serverTimeOffset)
  {
    // skip timers
    return false;
  }

  tag.SetDuration(XMLUtils::GetIntValue(pRecordingNode, "duration_seconds"));

  if (status == "Recording")
  {
    tag.SetDuration(tag.GetDuration() + 60 * XMLUtils::GetIntValue(pRecordingNode, "post_padding"));
  }

  if (status == "Ready" || status == "Pending" || status == "Recording")
  {
    if (!flatten)
    {
      buffer = kodi::tools::StringUtils::Format("/%s", title.c_str());
      tag.SetDirectory(buffer);
    }
    buffer.clear();
    if (XMLUtils::GetString(pRecordingNode, "desc", buffer))
    {
      tag.SetPlot(buffer);
    }
  }
  else if (status == "Failed")
  {
    buffer = kodi::tools::StringUtils::Format("/%s/%s", kodi::GetLocalizedString(30166).c_str(), title.c_str());
    tag.SetDirectory(buffer);
    if (XMLUtils::GetString(pRecordingNode, "reason", buffer))
    {
      tag.SetPlot(buffer);
    }
    if (tag.GetDuration() < 0)
    {
      tag.SetDuration(0);
    }
  }
  else if (status == "Conflict")
  {
    // shouldn't happen;
    return false;
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR, "Unknown status %s", status.c_str());
    return false;
  }

  // v4 users won't see recently concluded recordings on the EPG
  int endEpgTime = XMLUtils::GetIntValue(pRecordingNode, "epg_end_time_ticks");
  if (endEpgTime > time(nullptr) - 24 * 3600)
  {
    tag.SetEPGEventId(endEpgTime);
  }
  else if (status == "Recording" || status == "Pending")
  {
    // check for EPG based recording
    tag.SetEPGEventId(XMLUtils::GetIntValue(pRecordingNode, "epg_event_oid", PVR_TIMER_NO_EPG_UID));
    if (tag.GetEPGEventId() != PVR_TIMER_NO_EPG_UID)
    {
      tag.SetEPGEventId(tag.GetRecordingTime() + tag.GetDuration());
    }
  }

  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "id", buffer);
  tag.SetRecordingId(buffer);

  tag.SetSeriesNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
  tag.SetEpisodeNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);

  if (XMLUtils::GetString(pRecordingNode, "subtitle", buffer))
  {
    if (!m_settings.m_kodiLook)
    {
      tag.SetTitle(buffer);
    }
    if (ParseNextPVRSubtitle(pRecordingNode, tag))
    {
      if (m_settings.m_separateSeasons && multipleSeasons && tag.GetSeriesNumber() != PVR_RECORDING_INVALID_SERIES_EPISODE)
      {
        if (!m_settings.m_kodiLook)
        {
          tag.SetDirectory(kodi::tools::StringUtils::Format("/%s/%s %d", title.c_str(), kodi::GetLocalizedString(20373).c_str(), tag.GetSeriesNumber()));
          tag.SetSeriesNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
          tag.SetEpisodeNumber(PVR_RECORDING_INVALID_SERIES_EPISODE);
        }
        else
        {
          tag.SetDirectory(kodi::tools::StringUtils::Format("/%s/%s %d", tag.GetTitle().c_str(), kodi::GetLocalizedString(20373).c_str(), tag.GetSeriesNumber()));
        }
      }
    }
  }
  tag.SetYear(XMLUtils::GetIntValue(pRecordingNode, "year"));

  std::string original;
  XMLUtils::GetString(pRecordingNode, "original", original);
  tag.SetFirstAired(original);

  if (m_settings.m_backendResume)
  {
    tag.SetPlayCount(0);
    tag.SetLastPlayedPosition(XMLUtils::GetIntValue(pRecordingNode, "playback_position"));
    bool played = false;
    if (XMLUtils::GetBoolean(pRecordingNode, "played", played))
    {
      if (tag.GetLastPlayedPosition() >= tag.GetDuration() - 60)
      {
        tag.SetPlayCount(1);
        tag.SetLastPlayedPosition(0);
      }
    }
    m_lastPlayed[std::stoi(tag.GetRecordingId())] = tag.GetLastPlayedPosition();
    m_playCount[std::stoi(tag.GetRecordingId())] = tag.GetPlayCount();
  }


  tag.SetChannelUid(XMLUtils::GetIntValue(pRecordingNode, "channel_id"));
  if (tag.GetChannelUid() == 0)
    tag.SetChannelUid(PVR_CHANNEL_INVALID_UID);
  else
    tag.SetIconPath(g_pvrclient->m_channels.GetChannelIconFileName(tag.GetChannelUid()));

  buffer.clear();
  if (XMLUtils::GetString(pRecordingNode, "channel", buffer))
  {
    tag.SetChannelName(buffer);
  }
  tag.SetSizeInBytes(0);
  std::string recordingFile;
  if (XMLUtils::GetString(pRecordingNode, "file", recordingFile))
  {
    if (m_settings.m_showRecordingSize)
    {
      kodi::tools::StringUtils::Replace(recordingFile, '\\', '/');
      if (kodi::tools::StringUtils::StartsWith(recordingFile, "//"))
      {
        recordingFile = "smb:" + recordingFile;
      }
      if (kodi::vfs::FileExists(recordingFile))
      {
        kodi::vfs::CFile fileSize;
        if (fileSize.OpenFile(recordingFile))
        {
          tag.SetSizeInBytes(fileSize.GetLength());
          fileSize.Close();
        }
      }
      else
      {
        // don't play recording as file;
        recordingFile.clear();
      }
    }
  }

  m_hostFilenames[tag.GetRecordingId()] = recordingFile;

  // if we use unknown Kodi logs warning and turns it to TV so save some steps
  tag.SetChannelType(g_pvrclient->m_channels.GetChannelType(tag.GetChannelUid()));
  if (tag.GetChannelType() != PVR_RECORDING_CHANNEL_TYPE_RADIO)
  {
    std::string artworkPath;
    if (m_settings.m_backendVersion < 50000)
    {
      artworkPath = kodi::tools::StringUtils::Format("%s/service?method=recording.artwork&sid=%s&recording_id=%s", m_settings.m_urlBase, m_request.GetSID(), tag.GetRecordingId().c_str());
      tag.SetThumbnailPath(artworkPath);
      artworkPath = kodi::tools::StringUtils::Format("%s/service?method=recording.fanart&sid=%s&recording_id=%s", m_settings.m_urlBase, m_request.GetSID(), tag.GetRecordingId().c_str());
      tag.SetFanartPath(artworkPath);
    }
    else
    {
      if (m_settings.m_sendSidWithMetadata)
        artworkPath = kodi::tools::StringUtils::Format("%s/service?method=channel.show.artwork&sid=%s&name=%s", m_settings.m_urlBase, m_request.GetSID(), UriEncode(title).c_str());
      else
        artworkPath = kodi::tools::StringUtils::Format("%s/service?method=channel.show.artwork&name=%s", m_settings.m_urlBase, UriEncode(title).c_str());
      tag.SetFanartPath(artworkPath);
      artworkPath += "&prefer=poster";
      tag.SetThumbnailPath(artworkPath);
    }
  }
  if (XMLUtils::GetAdditiveString(pRecordingNode->FirstChildElement("genres"), "genre", EPG_STRING_TOKEN_SEPARATOR, buffer, true))
  {
    tag.SetGenreType(EPG_GENRE_USE_STRING);
    tag.SetGenreSubType(0);
    tag.SetGenreDescription(buffer);
  }

  std::string significance;
  XMLUtils::GetString(pRecordingNode, "significance", significance);
  if (significance.find("Premiere") != std::string::npos)
  {
    tag.SetFlags(PVR_RECORDING_FLAG_IS_PREMIERE);
  }
  else if (significance.find("Finale") != std::string::npos)
  {
    tag.SetFlags(PVR_RECORDING_FLAG_IS_FINALE);
  }

  return true;
}

bool Recordings::ParseNextPVRSubtitle(const tinyxml2::XMLNode *pRecordingNode, kodi::addon::PVRRecording& tag)
{
  std::string buffer;
  bool hasSeasonEpisode = false;
  if (XMLUtils::GetString(pRecordingNode, "subtitle", buffer))
  {
    std::regex base_regex("S(\\d{2,4})E(\\d+) - ?(.+)?");
    std::smatch base_match;
    // note NextPVR does not support S0 for specials
    if (std::regex_match(buffer, base_match, base_regex))
    {
      if (base_match.size() == 3 || base_match.size() == 4)
      {

        std::ssub_match base_sub_match = base_match[1];
        tag.SetSeriesNumber(std::stoi(base_sub_match.str()));
        base_sub_match = base_match[2];
        tag.SetEpisodeNumber(std::stoi(base_sub_match.str()));
        if (m_settings.m_kodiLook)
        {
          if (base_match.size() == 4)
          {
            base_sub_match = base_match[3];
            tag.SetEpisodeName(base_sub_match.str());
          }
        }
        hasSeasonEpisode = true;
      }
    }
    else if (m_settings.m_kodiLook)
    {
      tag.SetEpisodeName(buffer);
    }
  }

  if (!hasSeasonEpisode)
  {
    std::string recordingFile;
    if (XMLUtils::GetString(pRecordingNode, "file", recordingFile))
    {
      std::regex base_regex("S(\\d{2,4})E(\\d+)");
      std::smatch base_match;
      if (std::regex_search(recordingFile, base_match, base_regex))
      {
        if (base_match.size() == 3)
        {
          std::ssub_match base_sub_match = base_match[1];
          tag.SetSeriesNumber(std::stoi(base_sub_match.str()));
          base_sub_match = base_match[2];
          tag.SetEpisodeNumber(std::stoi(base_sub_match.str()));
          if (!m_settings.m_kodiLook)
          {
            tag.SetTitle(kodi::tools::StringUtils::Format("S%2.2dE%2.2d - %s", tag.GetSeriesNumber(), tag.GetEpisodeNumber(), buffer.c_str()));
          }
          hasSeasonEpisode = true;
        }
      }
    }
  }
  return hasSeasonEpisode;
}

PVR_ERROR Recordings::DeleteRecording(const kodi::addon::PVRRecording& recording)
{
  if (recording.GetRecordingTime() < time(nullptr) && recording.GetRecordingTime() + recording.GetDuration() > time(nullptr))
    return PVR_ERROR_RECORDING_RUNNING;

  const std::string request = "recording.delete&recording_id=" + recording.GetRecordingId();
  tinyxml2::XMLDocument doc;
  if ( m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    return PVR_ERROR_NO_ERROR;
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "DeleteRecording failed");
  }
  return PVR_ERROR_FAILED;
}

bool Recordings::ForgetRecording(const kodi::addon::PVRRecording& recording)
{
  // tell backend to forget recording history so it can re recorded.
  std::string request = "recording.forget&recording_id=";
  request.append(recording.GetRecordingId());
  tinyxml2::XMLDocument doc;
  return m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS;
}

//==============================================================================
/// SetRecordingPlayCount wil be called wwhen
/// Set watched - play count increases
/// Set unwatched - count set to 0,
/// Recording start - no change in count
/// Recording end when end is in playcountminimumpercent zone then the count
/// is incremented

PVR_ERROR Recordings::SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count)
{
  PVR_ERROR result = PVR_ERROR_NO_ERROR;
  int current = m_playCount[std::stoi(recording.GetRecordingId())];
  kodi::Log(ADDON_LOG_DEBUG, "Play count %s %d %d", recording.GetTitle().c_str(), count, current);
  if (count < current)
  {
    // unwatch count is zero.
    SetRecordingLastPlayedPosition(recording, 0);
    m_playCount[std::stoi(recording.GetRecordingId())] = count;
  }
  else
  {

  }
  return result;
}

//==============================================================================
/// SetRecordingLastPlayedPosition wil be called wwhen
/// Set watched - postion = 0, play count incremented.  Note it is not called if
/// the watched positions is already 0
/// Resume reset - position = 0
/// Recording start position = 0 at start of file
/// Recording end actual position or when end is in ignorepercentatend zone the
/// position is -1 with play count incremented
/// Set unwatched - Not called by core
///
PVR_ERROR Recordings::SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int lastplayedposition)
{

  int originalPosition = lastplayedposition;
  int current = m_playCount[std::stoi(recording.GetRecordingId())];
  if (recording.GetPlayCount() > current && lastplayedposition == 0)
  {
    // Kodi rolled the play count but didn't send EOF
    lastplayedposition = recording.GetDuration();
    m_playCount[std::stoi(recording.GetRecordingId())] = recording.GetPlayCount();
  }

  if ( m_lastPlayed[std::stoi(recording.GetRecordingId())] != lastplayedposition )
  {
    g_pvrclient->m_lastRecordingUpdateTime = std::numeric_limits<time_t>::max();
    time_t timerUpdate = m_timers.m_lastTimerUpdateTime;
    if (lastplayedposition == -1)
    {
      lastplayedposition = recording.GetDuration();
    }
    const std::string request = kodi::tools::StringUtils::Format("recording.watched.set&recording_id=%s&position=%d", recording.GetRecordingId().c_str(), lastplayedposition);
    tinyxml2::XMLDocument doc;
    if (m_request.DoMethodRequest(request, doc) != tinyxml2::XML_SUCCESS)
    {
      kodi::Log(ADDON_LOG_DEBUG, "SetRecordingLastPlayedPosition failed");
      return PVR_ERROR_FAILED;
    }
    if (m_settings.m_backendVersion >= 5007)
    {
      time_t lastUpdate;
      if (m_request.GetLastUpdate("recording.lastupdated&ignore_resume=true", lastUpdate) == tinyxml2::XML_SUCCESS)
      {
        if (timerUpdate >= lastUpdate)
        {
          if (m_request.GetLastUpdate("recording.lastupdated", lastUpdate) == tinyxml2::XML_SUCCESS)
          {
            // only change is watched point so skip it
            m_lastPlayed[std::stoi(recording.GetRecordingId())] = lastplayedposition;
            g_pvrclient->m_lastRecordingUpdateTime = lastUpdate;
          }
        }
      }
    }
    if ( g_pvrclient->m_lastRecordingUpdateTime == std::numeric_limits<time_t>::max())
      g_pvrclient->m_lastRecordingUpdateTime = 0;
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Recordings::GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position)
{
  position = m_lastPlayed[std::stoi(recording.GetRecordingId())];
  if (position == recording.GetDuration())
    position = 0;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Recordings::GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl)
{
  const std::string request = "recording.edl&recording_id=" + recording.GetRecordingId();
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    int numEDL = 0;
    tinyxml2::XMLNode* commercialsNode = doc.RootElement()->FirstChildElement("commercials");
    tinyxml2::XMLNode* pCommercialNode;
    for (pCommercialNode = commercialsNode->FirstChildElement("commercial"); pCommercialNode; pCommercialNode = pCommercialNode->NextSiblingElement())
    {
      if (numEDL++ == PVR_ADDON_EDL_LENGTH)
      {
        /* PVR core stores EDL breaks in a hard-coded array in PVRClient.cpp PVR_EDL_ENTRY edl_array[PVR_ADDON_EDL_LENGTH];
        * currently https://github.com/xbmc/xbmc/blob/master/xbmc/pvr/addons/PVRClient.cpp#L1056
        * This check avoids overflowing this arrray */
         
        kodi::Log(ADDON_LOG_WARNING, "Maximum EDL entries reached");
        break;
      }
      kodi::addon::PVREDLEntry entry;
      std::string buffer;
      XMLUtils::GetString(pCommercialNode, "start", buffer);
      entry.SetStart(std::stoll(buffer) * 1000);
      buffer.clear();
      XMLUtils::GetString(pCommercialNode, "end", buffer);
      entry.SetEnd(std::stoll(buffer) * 1000);
      entry.SetType(PVR_EDL_TYPE_COMBREAK);
      edl.emplace_back(entry);
    }
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_FAILED;
}

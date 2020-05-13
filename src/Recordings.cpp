/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Recordings.h"

#include "kodi/util/XMLUtils.h"

#include "pvrclient-nextpvr.h"

#include <regex>

#include <p8-platform/util/StringUtils.h>

using namespace NextPVR;

/************************************************************/
/** Record handling **/

int Recordings::GetNumRecordings(void)
{
  // need something more optimal, but this will do for now...
  // Return -1 on error.

  LOG_API_CALL(__FUNCTION__);
  if (m_iRecordingCount != 0)
    return m_iRecordingCount;

  std::string response;
  if (m_request.DoRequest("/service?method=recording.list&filter=ready", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      if (recordingsNode != nullptr)
      {
        TiXmlElement* pRecordingNode;
        m_iRecordingCount = 0;
        for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
        {
          m_iRecordingCount++;
        }
      }
    }
  }
  LOG_API_IRET(__FUNCTION__, m_iRecordingCount);
  return m_iRecordingCount;
}

PVR_ERROR Recordings::GetRecordings(ADDON_HANDLE handle)
{
  // include already-completed recordings
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  m_hostFilenames.clear();
  LOG_API_CALL(__FUNCTION__);
  int recordingCount = 0;
  std::string response;
  if (m_request.DoRequest("/service?method=recording.list&filter=all", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      //dump_to_log(&doc, 0);
      PVR_RECORDING tag;
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      TiXmlElement* pRecordingNode;
      std::map<std::string, int> names;
      if (m_settings.m_flattenRecording)
      {
        for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
        {
          std::string title;
          XMLUtils::GetString(pRecordingNode, "name", title);
          names[title]++;
        }
      }
      for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        tag = {};
        std::string title;
        XMLUtils::GetString(pRecordingNode, "name", title);
        if (UpdatePvrRecording(pRecordingNode, &tag, title, names[title] == 1))
        {
          recordingCount++;
          PVR->TransferRecordingEntry(handle, &tag);
        }
      }
    }
    m_iRecordingCount = recordingCount;
    XBMC->Log(LOG_DEBUG, "Updated recordings %lld", g_pvrclient->m_lastRecordingUpdateTime);
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  g_pvrclient->m_lastRecordingUpdateTime = time(0);
  LOG_API_IRET(__FUNCTION__, returnValue);
  return returnValue;
}

bool Recordings::UpdatePvrRecording(TiXmlElement* pRecordingNode, PVR_RECORDING* tag, const std::string& title, bool flatten)
{
  std::string buffer;
  title.copy(tag->strTitle, sizeof(tag->strTitle) - 1);

  tag->recordingTime = atol(pRecordingNode->FirstChildElement("start_time_ticks")->FirstChild()->Value());

  std::string status;
  XMLUtils::GetString(pRecordingNode, "status", status);
  if (status == "Pending" && tag->recordingTime > time(nullptr) + m_settings.m_serverTimeOffset)
  {
    // skip timers
    return false;
  }
  tag->iDuration = atoi(pRecordingNode->FirstChildElement("duration_seconds")->FirstChild()->Value());

  if (status == "Ready" || status == "Pending" || status == "Recording")
  {
    if (!flatten)
    {
      buffer = StringUtils::Format("/%s", title.c_str());
      buffer.copy(tag->strDirectory, sizeof(tag->strDirectory) - 1);
    }
    buffer.clear();
    if (XMLUtils::GetString(pRecordingNode, "desc", buffer))
    {
      buffer.copy(tag->strPlot, sizeof(tag->strPlot) - 1);
    }
  }
  else if (status == "Failed")
  {
    buffer = StringUtils::Format("/%s/%s", XBMC->GetLocalizedString(30166), title.c_str());
    buffer.copy(tag->strDirectory, sizeof(tag->strDirectory) - 1);
    if (XMLUtils::GetString(pRecordingNode, "reason", buffer))
    {
      buffer.copy(tag->strPlot, sizeof(tag->strPlot) - 1);
    }
    if (tag->iDuration < 0)
    {
      tag->iDuration = 0;
    }
  }
  else if (status == "Conflict")
  {
    // shouldn't happen;
    return false;
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Unknown status %s", status.c_str());
    return false;
  }

  if (status == "Recording" || status == "Pending")
  {
    tag->iEpgEventId = g_pvrclient->XmlGetInt(pRecordingNode, "epg_event_oid", PVR_TIMER_NO_EPG_UID);
    if (tag->iEpgEventId != PVR_TIMER_NO_EPG_UID)
    {
      // EPG Event ID is likely not valid on most older recordings so current or pending only
      // Linked tags need to be on end time because recordingTime can change because of pre-padding

      tag->iEpgEventId = tag->recordingTime + tag->iDuration;
    }
  }

  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "id", buffer);
  buffer.copy(tag->strRecordingId, buffer.length());

  tag->iSeriesNumber = PVR_RECORDING_INVALID_SERIES_EPISODE;
  tag->iEpisodeNumber = PVR_RECORDING_INVALID_SERIES_EPISODE;

  if (XMLUtils::GetString(pRecordingNode, "subtitle", buffer))
  {
    if (m_settings.m_kodiLook || flatten)
    {
      ParseNextPVRSubtitle(buffer, tag);
    }
    else
    {
      buffer.copy(tag->strTitle, sizeof(tag->strTitle) - 1);
    }
  }
  int lookup = 0;
  tag->iLastPlayedPosition = g_pvrclient->XmlGetInt(pRecordingNode,"playback_position",lookup);
  if (XMLUtils::GetInt(pRecordingNode, "channel_id", lookup))
  {
    if (lookup == 0)
    {
      tag->iChannelUid = PVR_CHANNEL_INVALID_UID;
    }
    else
    {
      tag->iChannelUid = lookup;
      strcpy(tag->strIconPath, g_pvrclient->m_channels.GetChannelIconFileName(tag->iChannelUid).c_str());
    }
  }
  else
  {
    tag->iChannelUid = PVR_CHANNEL_INVALID_UID;
  }
  if (XMLUtils::GetString(pRecordingNode, "channel", buffer))
  {
    buffer.copy(tag->strChannelName, buffer.length());
  }

  std::string recordingFile;
  if (XMLUtils::GetString(pRecordingNode, "file", recordingFile))
  {
    if (m_settings.m_showRecordingSize)
    {
      StringUtils::Replace(recordingFile, '\\', '/');
      if (StringUtils::StartsWith(recordingFile, "//"))
      {
        recordingFile = "smb:" + recordingFile;
      }
      if (XBMC->FileExists(recordingFile.c_str(), READ_NO_CACHE))
      {
        void* fileHandle = XBMC->OpenFile(recordingFile.c_str(), READ_NO_CACHE);
        tag->sizeInBytes = XBMC->GetFileLength(fileHandle);
        XBMC->CloseFile(fileHandle);
      }
      else
      {
        // don't play recording as file
        recordingFile.clear();
      }
    }
  }

  m_hostFilenames[tag->strRecordingId] = recordingFile;

  // if we use unknown Kodi logs warning and turns it to TV so save some steps
  tag->channelType = g_pvrclient->m_channels.GetChannelType(tag->iChannelUid);
  if (tag->channelType != PVR_RECORDING_CHANNEL_TYPE_RADIO)
  {
    std::string artworkPath;
    if (m_settings.m_backendVersion < 50000)
    {
      artworkPath = StringUtils::Format("http://%s:%d/service?method=recording.artwork&sid=%s&recording_id=%s", m_settings.m_hostname.c_str(), m_settings.m_port, g_pvrclient->m_sid, tag->strRecordingId);
      artworkPath.copy(tag->strThumbnailPath, sizeof(tag->strThumbnailPath) - 1);
      artworkPath = StringUtils::Format("http://%s:%d/service?method=recording.fanart&sid=%s&recording_id=%s", m_settings.m_hostname.c_str(), m_settings.m_port, g_pvrclient->m_sid, tag->strRecordingId);
      artworkPath.copy(tag->strFanartPath, sizeof(tag->strFanartPath) - 1);
    }
    else
    {
      if (m_settings.m_sendSidWithMetadata)
        artworkPath = StringUtils::Format("http://%s:%d/service?method=channel.show.artwork&sid=%s&name=%s", m_settings.m_hostname.c_str(), m_settings.m_port, g_pvrclient->m_sid, UriEncode(title).c_str());
      else
        artworkPath = StringUtils::Format("http://%s:%d/service?method=channel.show.artwork&name=%s", m_settings.m_hostname.c_str(), m_settings.m_port, UriEncode(title).c_str());
      artworkPath.copy(tag->strFanartPath, sizeof(tag->strFanartPath) - 1);
      artworkPath += "&prefer=poster";
      artworkPath.copy(tag->strThumbnailPath, sizeof(tag->strThumbnailPath) - 1);
    }
  }
  if (GetAdditiveString(pRecordingNode->FirstChildElement("genres"), "genre", EPG_STRING_TOKEN_SEPARATOR, buffer, true))
  {
    tag->iGenreType = EPG_GENRE_USE_STRING;
    tag->iGenreSubType = 0;
    buffer.copy(tag->strGenreDescription, sizeof(tag->strGenreDescription) - 1);
  }

  std::string significance;
  XMLUtils::GetString(pRecordingNode, "significance", significance);
  if (significance.find("Premiere") != std::string::npos)
  {
    tag->iFlags = PVR_RECORDING_FLAG_IS_PREMIERE;
  }
  else if (significance.find("Finale") != std::string::npos)
  {
    tag->iFlags = PVR_RECORDING_FLAG_IS_FINALE;
  }

  bool played = false;
  if (XMLUtils::GetBoolean(pRecordingNode, "played", played))
  {
    tag->iPlayCount = 1;
  }

  return true;
}

bool Recordings::GetAdditiveString(const TiXmlNode* pRootNode, const char* strTag, const std::string& strSeparator, std::string& strStringValue, bool clear)
{
  bool bResult = false;
  if (pRootNode != nullptr)
  {
    std::string strTemp;
    const TiXmlElement* node = pRootNode->FirstChildElement(strTag);
    if (node && node->FirstChild() && clear)
      strStringValue.clear();
    while (node)
    {
      if (node->FirstChild())
      {
        bResult = true;
        strTemp = node->FirstChild()->Value();
        const char* clear = node->Attribute("clear");
        if (strStringValue.empty() || (clear && StringUtils::CompareNoCase(clear, "true") == 0))
          strStringValue = strTemp;
        else
          strStringValue += strSeparator + strTemp;
      }
      node = node->NextSiblingElement(strTag);
    }
  }

  return bResult;
}

void Recordings::ParseNextPVRSubtitle(const std::string episodeName, PVR_RECORDING* tag)
{
  std::regex base_regex("S(\\d\\d)E(\\d+) - ?(.+)?");
  std::smatch base_match;
  // note NextPVR does not support S0 for specials
  if (std::regex_match(episodeName, base_match, base_regex))
  {
    if (base_match.size() == 3 || base_match.size() == 4)
    {
      std::ssub_match base_sub_match = base_match[1];
      tag->iSeriesNumber = std::stoi(base_sub_match.str());
      base_sub_match = base_match[2];
      tag->iEpisodeNumber = std::stoi(base_sub_match.str());
      if (base_match.size() == 4)
      {
        base_sub_match = base_match[3];
        strcpy(tag->strEpisodeName, base_sub_match.str().c_str());
      }
    }
  }
  else
  {
    episodeName.copy(tag->strEpisodeName, sizeof(tag->strEpisodeName) - 1);
  }
}

PVR_ERROR Recordings::DeleteRecording(const PVR_RECORDING& recording)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "DeleteRecording");

  if (recording.recordingTime < time(nullptr) && recording.recordingTime + recording.iDuration > time(nullptr))
    return PVR_ERROR_RECORDING_RUNNING;

  const std::string request = StringUtils::Format("/service?method=recording.delete&recording_id=%s", recording.strRecordingId);

  std::string response;
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      return PVR_ERROR_NO_ERROR;
    }
  }
  else
  {
    XBMC->Log(LOG_DEBUG, "DeleteRecording failed");
  }
  return PVR_ERROR_FAILED;
}

bool Recordings::ForgetRecording(const PVR_RECORDING& recording)
{
  LOG_API_CALL(__FUNCTION__);
  // tell backend to forget recording history so it can re recorded.
  std::string request = "/service?method=recording.forget&recording_id=";
  request.append(recording.strRecordingId);
  std::string response;
  return m_request.DoRequest(request.c_str(), response) == HTTP_OK;
}

PVR_ERROR Recordings::SetRecordingLastPlayedPosition(const PVR_RECORDING& recording, int lastplayedposition)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "SetRecordingLastPlayedPosition");
  const std::string request = StringUtils::Format("/service?method=recording.watched.set&recording_id=%s&position=%d", recording.strRecordingId, lastplayedposition);

  std::string response;
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">") == nullptr)
    {
      XBMC->Log(LOG_DEBUG, "SetRecordingLastPlayedPosition failed");
      return PVR_ERROR_FAILED;
    }
    g_pvrclient->m_lastRecordingUpdateTime = 0;
  }
  return PVR_ERROR_NO_ERROR;
}

int Recordings::GetRecordingLastPlayedPosition(const PVR_RECORDING& recording)
{
  LOG_API_CALL(__FUNCTION__);
  return recording.iLastPlayedPosition;
}

PVR_ERROR Recordings::GetRecordingEdl(const PVR_RECORDING& recording, PVR_EDL_ENTRY entries[], int* size)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "GetRecordingEdl");
  const std::string request = StringUtils::Format("/service?method=recording.edl&recording_id=%s", recording.strRecordingId);

  std::string response;
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">") != nullptr)
    {
      TiXmlDocument doc;
      if (doc.Parse(response.c_str()) != nullptr)
      {
        int index = 0;
        TiXmlElement* commercialsNode = doc.RootElement()->FirstChildElement("commercials");
        TiXmlElement* pCommercialNode;
        for (pCommercialNode = commercialsNode->FirstChildElement("commercial"); pCommercialNode; pCommercialNode = pCommercialNode->NextSiblingElement())
        {
          PVR_EDL_ENTRY entry;
          std::string buffer;
          XMLUtils::GetString(pCommercialNode,"start",buffer);
          entry.start = std::stoll(buffer) * 1000;
          buffer.clear();
          XMLUtils::GetString(pCommercialNode,"end",buffer);
          entry.end = std::stoll(buffer) * 1000;
          entry.type = PVR_EDL_TYPE_COMBREAK;
          entries[index] = entry;
          index++;
        }
        *size = index;
        return PVR_ERROR_NO_ERROR;
      }
    }
  }
  return PVR_ERROR_FAILED;
}

/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Timers.h"

#include "kodi/util/XMLUtils.h"

#include "pvrclient-nextpvr.h"

#include <p8-platform/util/StringUtils.h>

using namespace NextPVR;

/************************************************************/
/** Timer handling */

int Timers::GetNumTimers(void)
{
  LOG_API_CALL(__FUNCTION__);
  // Return -1 in case of error.
  if (m_iTimerCount != -1)
    return m_iTimerCount;

  std::string response;
  int timerCount = -1;
  // get list of recurring recordings
  if (m_request.DoRequest("/service?method=recording.recurring.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recurrings");
      if (recordingsNode != nullptr)
      {
        TiXmlElement* pRecordingNode;
        for (pRecordingNode = recordingsNode->FirstChildElement("recurring"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
        {
          timerCount++;
        }
      }
    }
  }


  // get list of pending recordings
  response = "";
  if (m_request.DoRequest("/service?method=recording.list&filter=pending", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      if (recordingsNode != nullptr)
      {
        TiXmlElement* pRecordingNode;
        for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
        {
          timerCount++;
        }
      }
    }
  }
  if (timerCount > -1)
  {
    m_iTimerCount = timerCount + 1;
  }
  LOG_API_IRET(__FUNCTION__, timerCount);
  return m_iTimerCount;
}

PVR_ERROR Timers::GetTimers(ADDON_HANDLE& handle)
{
  std::string response;
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  LOG_API_CALL(__FUNCTION__);
  int timerCount = 0;
  // first add the recurring recordings
  if (m_request.DoRequest("/service?method=recording.recurring.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      PVR_TIMER tag;
      TiXmlElement* recurringsNode = doc.RootElement()->FirstChildElement("recurrings");
      TiXmlElement* pRecurringNode;
      for (pRecurringNode = recurringsNode->FirstChildElement("recurring"); pRecurringNode; pRecurringNode = pRecurringNode->NextSiblingElement())
      {
        tag = {0};
        TiXmlElement* pMatchRulesNode = pRecurringNode->FirstChildElement("matchrules");
        TiXmlElement* pRulesNode = pMatchRulesNode->FirstChildElement("Rules");

        tag.iClientIndex = g_pvrclient->XmlGetUInt(pRecurringNode, "id");
        tag.iClientChannelUid = g_pvrclient->XmlGetInt(pRecurringNode, "ChannelOID");

        tag.iTimerType = pRulesNode->FirstChildElement("EPGTitle") ? TIMER_REPEATING_EPG : TIMER_REPEATING_MANUAL;

        // start/end time

        std::string buffer;
        if (XMLUtils::GetString(pRulesNode, "StartTimeTicks", buffer))
        {
          tag.startTime = stoll(buffer);
          if (tag.startTime < time(nullptr))
          {
            tag.startTime = 0;
          }
          else
          {
            if (XMLUtils::GetString(pRulesNode, "EndTimeTicks", buffer))
              tag.endTime = stoll(buffer);
          }
        }

        // keyword recordings
        std::string advancedRulesText;
        if (XMLUtils::GetString(pRulesNode, "AdvancedRules", advancedRulesText))
        {
          if (advancedRulesText.find("KEYWORD: ") != std::string::npos)
          {
            tag.iTimerType = TIMER_REPEATING_KEYWORD;
            tag.startTime = 0;
            tag.endTime = 0;
            tag.bStartAnyTime = true;
            tag.bEndAnyTime = true;
            strncpy(tag.strEpgSearchString, &advancedRulesText.c_str()[9], sizeof(tag.strEpgSearchString) - 1);
          }
          else
          {
            tag.iTimerType = TIMER_REPEATING_ADVANCED;
            tag.startTime = 0;
            tag.endTime = 0;
            tag.bStartAnyTime = true;
            tag.bEndAnyTime = true;
            tag.bFullTextEpgSearch = true;
            strncpy(tag.strEpgSearchString, advancedRulesText.c_str(), sizeof(tag.strEpgSearchString) - 1);
          }

        }

        // days
        tag.iWeekdays = PVR_WEEKDAY_ALLDAYS;
        std::string daysText;
        if (XMLUtils::GetString(pRulesNode, "Days", daysText))
        {
          tag.iWeekdays = PVR_WEEKDAY_NONE;
          if (daysText.find("SUN") != std::string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_SUNDAY;
          if (daysText.find("MON") != std::string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_MONDAY;
          if (daysText.find("TUE") != std::string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_TUESDAY;
          if (daysText.find("WED") != std::string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_WEDNESDAY;
          if (daysText.find("THU") != std::string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_THURSDAY;
          if (daysText.find("FRI") != std::string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_FRIDAY;
          if (daysText.find("SAT") != std::string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_SATURDAY;
        }

        // pre/post padding
        tag.iMarginStart = g_pvrclient->XmlGetUInt(pRecurringNode, "PrePadding");
        tag.iMarginEnd = g_pvrclient->XmlGetUInt(pRecurringNode, "PostPadding");

        // number of recordings to keep
        tag.iMaxRecordings = g_pvrclient->XmlGetInt(pRecurringNode, "Keep");

        // prevent duplicates
        bool duplicate;
        if (XMLUtils::GetBoolean(pRulesNode, "OnlyNewEpisodes", duplicate))
        {
          if (duplicate == true)
          {
            tag.iPreventDuplicateEpisodes = 1;
          }
        }

        std::string recordingDirectoryID;
        if (XMLUtils::GetString(pRulesNode, "RecordingDirectoryID", recordingDirectoryID))
        {
          int i = 0;
          for (auto it = m_settings.m_recordingDirectories.begin(); it != m_settings.m_recordingDirectories.end(); ++it, i++)
          {
            std::string bracketed = "[" + m_settings.m_recordingDirectories[i] + "]";
            if (bracketed == recordingDirectoryID)
            {
              tag.iRecordingGroup = i;
              break;
            }
          }
        }

        buffer.clear();
        XMLUtils::GetString(pRecurringNode, "name", buffer);
        buffer.copy(tag.strTitle, sizeof(tag.strTitle) - 1);

        tag.state = PVR_TIMER_STATE_SCHEDULED;

        strcpy(tag.strSummary, "summary");

        // pass timer to xbmc
        timerCount++;
        PVR->TransferTimerEntry(handle, &tag);
      }
    }
    // next add the one-off recordings.
    response = "";
    if (m_request.DoRequest("/service?method=recording.list&filter=pending", response) == HTTP_OK)
    {
      TiXmlDocument doc;
      if (doc.Parse(response.c_str()) != nullptr)
      {
        PVR_TIMER tag;

        TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
        for (TiXmlElement* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
        {
          tag = {0};
          UpdatePvrTimer(pRecordingNode, &tag);
          // pass timer to xbmc
          timerCount++;
          PVR->TransferTimerEntry(handle, &tag);
        }
      }
      response = "";
      if (m_request.DoRequest("/service?method=recording.list&filter=conflict", response) == HTTP_OK)
      {
        TiXmlDocument doc;
        if (doc.Parse(response.c_str()) != nullptr)
        {
          PVR_TIMER tag;
          TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
          for (TiXmlElement* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
          {
            tag = {0};
            UpdatePvrTimer(pRecordingNode, &tag);
            // pass timer to xbmc
            timerCount++;
            PVR->TransferTimerEntry(handle, &tag);
          }
          m_iTimerCount = timerCount;
        }
      }
    }
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }

  LOG_API_IRET(__FUNCTION__, returnValue);
  return returnValue;
}

bool Timers::UpdatePvrTimer(TiXmlElement* pRecordingNode, PVR_TIMER* tag)
{
  tag->iTimerType = pRecordingNode->FirstChildElement("epg_event_oid") ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL;

  tag->iClientIndex = g_pvrclient->XmlGetUInt(pRecordingNode, "id");
  tag->iClientChannelUid = g_pvrclient->XmlGetUInt(pRecordingNode, "channel_id");
  tag->iParentClientIndex = g_pvrclient->XmlGetUInt(pRecordingNode, "recurring_parent", PVR_TIMER_NO_PARENT);

  if (tag->iParentClientIndex != PVR_TIMER_NO_PARENT)
  {
    if (tag->iTimerType == TIMER_ONCE_EPG)
      tag->iTimerType = TIMER_ONCE_EPG_CHILD;
    else
      tag->iTimerType = TIMER_ONCE_MANUAL_CHILD;
  }
  tag->iEpgUid = g_pvrclient->XmlGetUInt(pRecordingNode, "epg_event_oid", PVR_TIMER_NO_EPG_UID);
  if (tag->iEpgUid != PVR_TIMER_NO_EPG_UID)
  {
    XBMC->Log(LOG_DEBUG, "Setting timer epg id %d %d", tag->iClientIndex, tag->iEpgUid);
  }

  tag->iMarginStart = g_pvrclient->XmlGetUInt(pRecordingNode, "pre_padding");
  tag->iMarginEnd = g_pvrclient->XmlGetUInt(pRecordingNode, "post_padding");

  std::string buffer;

  // name
  XMLUtils::GetString(pRecordingNode, "name", buffer);
  buffer.copy(tag->strTitle, sizeof(tag->strTitle) - 1);

  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "desc", buffer);
  buffer.copy(tag->strSummary, sizeof(tag->strSummary) - 1);
  // start/end time
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "start_time_ticks", buffer);
  buffer.resize(10);
  tag->startTime = std::stoll(buffer);
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "duration_seconds", buffer);
  tag->endTime = tag->startTime + std::stoll(buffer);

  tag->state = PVR_TIMER_STATE_SCHEDULED;

  std::string status;
  if (XMLUtils::GetString(pRecordingNode, "status", status))
  {
    if (status == "Recording" || (status == "Pending" && tag->startTime < time(nullptr) + m_settings.m_serverTimeOffset))
    {
      tag->state = PVR_TIMER_STATE_RECORDING;
    }
    else if (status == "Conflict")
    {
      tag->state = PVR_TIMER_STATE_CONFLICT_NOK;
    }
  }

  return true;
}

namespace
{
  struct TimerType : PVR_TIMER_TYPE
  {
    TimerType(unsigned int id,
              unsigned int attributes,
              const std::string& description,
              const std::vector<std::pair<int, std::string>>& maxRecordingsValues,
              int maxRecordingsDefault,
              const std::vector<std::pair<int, std::string>>& dupEpisodesValues,
              int dupEpisodesDefault,
              const std::vector<std::pair<int, std::string>>& recordingGroupsValues,
              int recordingGroupDefault)
    {
      memset(this, 0, sizeof(PVR_TIMER_TYPE));

      iId = id;
      iAttributes = attributes;
      iMaxRecordingsSize = static_cast<unsigned int>(maxRecordingsValues.size());
      iMaxRecordingsDefault = maxRecordingsDefault;
      iPreventDuplicateEpisodesSize = static_cast<unsigned int>(dupEpisodesValues.size());
      iPreventDuplicateEpisodesDefault = dupEpisodesDefault;
      iRecordingGroupSize = static_cast<unsigned int>(recordingGroupsValues.size());
      iRecordingGroupDefault = recordingGroupDefault;
      strncpy(strDescription, description.c_str(), sizeof(strDescription) - 1);

      int i = 0;
      for (auto it = maxRecordingsValues.begin(); it != maxRecordingsValues.end(); ++it, ++i)
      {
        maxRecordings[i].iValue = it->first;
        strncpy(maxRecordings[i].strDescription, it->second.c_str(), sizeof(maxRecordings[i].strDescription) - 1);
      }

      i = 0;
      for (auto it = dupEpisodesValues.begin(); it != dupEpisodesValues.end(); ++it, ++i)
      {
        preventDuplicateEpisodes[i].iValue = it->first;
        strncpy(preventDuplicateEpisodes[i].strDescription, it->second.c_str(), sizeof(preventDuplicateEpisodes[i].strDescription) - 1);
      }

      i = 0;
      for (auto it = recordingGroupsValues.begin(); it != recordingGroupsValues.end(); ++it, ++i)
      {
        recordingGroup[i].iValue = it->first;
        strncpy(recordingGroup[i].strDescription, it->second.c_str(), sizeof(recordingGroup[i].strDescription) - 1);
      }
    }
  };

} // unnamed namespace

PVR_ERROR Timers::GetTimerTypes(PVR_TIMER_TYPE types[], int* size)
{
  LOG_API_CALL(__FUNCTION__);
  static const int MSG_ONETIME_MANUAL = 30140;
  static const int MSG_ONETIME_GUIDE = 30141;
  static const int MSG_REPEATING_MANUAL = 30142;
  static const int MSG_REPEATING_GUIDE = 30143;
  static const int MSG_REPEATING_CHILD = 30144;
  static const int MSG_REPEATING_KEYWORD = 30145;
  static const int MSG_REPEATING_ADVANCED = 30171;

  static const int MSG_KEEPALL = 30150;
  static const int MSG_KEEP1 = 30151;
  static const int MSG_KEEP2 = 30152;
  static const int MSG_KEEP3 = 30153;
  static const int MSG_KEEP4 = 30154;
  static const int MSG_KEEP5 = 30155;
  static const int MSG_KEEP6 = 30156;
  static const int MSG_KEEP7 = 30157;
  static const int MSG_KEEP10 = 30158;

  static const int MSG_SHOWTYPE_FIRSTRUNONLY = 30160;
  static const int MSG_SHOWTYPE_ANY = 30161;

  /* PVR_Timer.iMaxRecordings values and presentation. */
  static std::vector<std::pair<int, std::string>> recordingLimitValues;
  if (recordingLimitValues.size() == 0)
  {
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_ASMANY, XBMC->GetLocalizedString(MSG_KEEPALL)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_1, XBMC->GetLocalizedString(MSG_KEEP1)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_2, XBMC->GetLocalizedString(MSG_KEEP2)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_3, XBMC->GetLocalizedString(MSG_KEEP3)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_4, XBMC->GetLocalizedString(MSG_KEEP4)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_5, XBMC->GetLocalizedString(MSG_KEEP5)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_6, XBMC->GetLocalizedString(MSG_KEEP6)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_7, XBMC->GetLocalizedString(MSG_KEEP7)));
    recordingLimitValues.emplace_back(std::make_pair(NEXTPVR_LIMIT_10, XBMC->GetLocalizedString(MSG_KEEP10)));
  }

  /* PVR_Timer.iPreventDuplicateEpisodes values and presentation.*/
  static std::vector<std::pair<int, std::string>> showTypeValues;
  if (showTypeValues.size() == 0)
  {
    showTypeValues.emplace_back(std::make_pair(NEXTPVR_SHOWTYPE_FIRSTRUNONLY, XBMC->GetLocalizedString(MSG_SHOWTYPE_FIRSTRUNONLY)));
    showTypeValues.emplace_back(std::make_pair(NEXTPVR_SHOWTYPE_ANY, XBMC->GetLocalizedString(MSG_SHOWTYPE_ANY)));
  }

  /* PVR_Timer.iRecordingGroup values and presentation */
  int i = 0;
  static std::vector<std::pair<int, std::string>> recordingGroupValues;
  for (auto it = m_settings.m_recordingDirectories.begin(); it != m_settings.m_recordingDirectories.end(); ++it, i++)
  {
    recordingGroupValues.emplace_back(std::make_pair(i, m_settings.m_recordingDirectories[i]));
  }

  static const unsigned int TIMER_MANUAL_ATTRIBS
    = PVR_TIMER_TYPE_IS_MANUAL |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_EPG_ATTRIBS
    = PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_REPEATING_MANUAL_ATTRIBS
    = PVR_TIMER_TYPE_IS_REPEATING |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_REPEATING_EPG_ATTRIBS
    = PVR_TIMER_TYPE_IS_REPEATING |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
      PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_CHILD_ATTRIBUTES
    = PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES;

  static const unsigned int TIMER_KEYWORD_ATTRIBS
    = PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_REPEATING_KEYWORD_ATTRIBS
      = PVR_TIMER_TYPE_IS_REPEATING |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
      PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_ADVANCED_ATTRIBS
    = PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH  |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;



  /* Timer types definition.*/
  static std::vector<std::unique_ptr<TimerType>> timerTypes;
  if (timerTypes.size() == 0)
  {
    timerTypes.emplace_back(
        /* One-shot manual (time and channel based) */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_MANUAL,
            /* Attributes. */
            TIMER_MANUAL_ATTRIBS,
            /* Description. */
            XBMC->GetLocalizedString(MSG_ONETIME_MANUAL), // "One time (manual)",
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));

    timerTypes.emplace_back(
        /* One-shot epg based */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_EPG,
            /* Attributes. */
            TIMER_EPG_ATTRIBS,
            /* Description. */
            XBMC->GetLocalizedString(MSG_ONETIME_GUIDE), // "One time (guide)",
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));

    timerTypes.emplace_back(
        /* Repeating manual (time and channel based) Parent */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_REPEATING_MANUAL,
            /* Attributes. */
            TIMER_MANUAL_ATTRIBS | TIMER_REPEATING_MANUAL_ATTRIBS,
            /* Description. */
            XBMC->GetLocalizedString(MSG_REPEATING_MANUAL), // "Repeating (manual)"
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));

    timerTypes.emplace_back(
        /* Repeating epg based Parent*/
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_REPEATING_EPG,
            /* Attributes. */
            TIMER_EPG_ATTRIBS | TIMER_REPEATING_EPG_ATTRIBS,
            /* Description. */
            XBMC->GetLocalizedString(MSG_REPEATING_GUIDE), // "Repeating (guide)"
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));

    timerTypes.emplace_back(
        /* Read-only one-shot for timers generated by timerec */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_MANUAL_CHILD,
            /* Attributes. */
            TIMER_MANUAL_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
            /* Description. */
            XBMC->GetLocalizedString(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));

    timerTypes.emplace_back(
        /* Read-only one-shot for timers generated by autorec */
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_ONCE_EPG_CHILD,
            /* Attributes. */
            TIMER_EPG_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
            /* Description. */
            XBMC->GetLocalizedString(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));

    timerTypes.emplace_back(
        /* Repeating epg based Parent*/
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_REPEATING_KEYWORD,
            /* Attributes. */
            TIMER_KEYWORD_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
            /* Description. */
            XBMC->GetLocalizedString(MSG_REPEATING_KEYWORD), // "Repeating (keyword)"
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));

    timerTypes.emplace_back(
        /* Repeating epg based Parent*/
        std::unique_ptr<TimerType>(new TimerType(
            /* Type id. */
            TIMER_REPEATING_ADVANCED,
            /* Attributes. */
            TIMER_ADVANCED_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
            /* Description. */
            XBMC->GetLocalizedString(MSG_REPEATING_ADVANCED), // "Repeating (advanced)"
            /* Values definitions for attributes. */
            recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0)));
  }

  /* Copy data to target array. */
  i = 0;
  for (auto it = timerTypes.begin(); it != timerTypes.end(); ++it, ++i)
    types[i] = **it;

  *size = static_cast<int>(timerTypes.size());

  return PVR_ERROR_NO_ERROR;
}

std::string Timers::GetDayString(int dayMask)
{
  std::string days;
  if (dayMask == (PVR_WEEKDAY_SATURDAY | PVR_WEEKDAY_SUNDAY))
  {
    days = "WEEKENDS";
  }
  else if (dayMask == (PVR_WEEKDAY_MONDAY | PVR_WEEKDAY_TUESDAY | PVR_WEEKDAY_WEDNESDAY | PVR_WEEKDAY_THURSDAY | PVR_WEEKDAY_FRIDAY))
  {
    days = "WEEKDAYS";
  }
  else
  {
    if (dayMask & PVR_WEEKDAY_SATURDAY)
      days += "SAT:";
    if (dayMask & PVR_WEEKDAY_SUNDAY)
      days += "SUN:";
    if (dayMask & PVR_WEEKDAY_MONDAY)
      days += "MON:";
    if (dayMask & PVR_WEEKDAY_TUESDAY)
      days += "TUE:";
    if (dayMask & PVR_WEEKDAY_WEDNESDAY)
      days += "WED:";
    if (dayMask & PVR_WEEKDAY_THURSDAY)
      days += "THU:";
    if (dayMask & PVR_WEEKDAY_FRIDAY)
      days += "FRI:";
  }

  return days;
}

PVR_ERROR Timers::AddTimer(const PVR_TIMER& timerinfo)
{

  char preventDuplicates[16];
  LOG_API_CALL(__FUNCTION__);
  if (timerinfo.iPreventDuplicateEpisodes)
    strcpy(preventDuplicates, "true");
  else
    strcpy(preventDuplicates, "false");

  const std::string encodedName = UriEncode(timerinfo.strTitle);
  const std::string encodedKeyword = UriEncode(timerinfo.strEpgSearchString);
  const std::string days = GetDayString(timerinfo.iWeekdays);
  const std::string directory = UriEncode(m_settings.m_recordingDirectories[timerinfo.iRecordingGroup]);
  const std::string oidKey = std::to_string(timerinfo.iEpgUid) + ":" + std::to_string(timerinfo.iClientChannelUid);
  const int epgOid = m_epgOidLookup[oidKey];
  XBMC->Log(LOG_DEBUG, "TIMER_%d %s", epgOid, oidKey.c_str());
  std::string request;
  switch (timerinfo.iTimerType)
  {
  case TIMER_ONCE_MANUAL:
    XBMC->Log(LOG_DEBUG, "TIMER_ONCE_MANUAL");
    // build one-off recording request
    request = StringUtils::Format("/service?method=recording.save&name=%s&channel=%d&time_t=%d&duration=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)(timerinfo.endTime - timerinfo.startTime),
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      directory.c_str()
      );
    break;

  case TIMER_ONCE_EPG:
    XBMC->Log(LOG_DEBUG, "TIMER_ONCE_EPG");
    // build one-off recording request
    request = StringUtils::Format("/service?method=recording.save&recording_id=%d&event_id=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      timerinfo.iClientIndex,
      epgOid,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      directory.c_str());
    break;

  case TIMER_REPEATING_EPG:
    if (timerinfo.iClientChannelUid == PVR_TIMER_ANY_CHANNEL)
    {
      // Fake a manual recording
      XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_EPG ANY CHANNEL");
      std::string title = encodedName + "%";
      request = StringUtils::Format("/service?method=recording.recurring.save&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s,&keyword=%s",
        encodedName.c_str(),
        timerinfo.iClientChannelUid,
        (int)timerinfo.startTime,
        (int)timerinfo.endTime,
        (int)timerinfo.iMaxRecordings,
        (int)timerinfo.iMarginStart,
        (int)timerinfo.iMarginEnd,
        days.c_str(),
        directory.c_str(),
        title.c_str()
        );
    }
    else
    {
      XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_EPG");
      // build recurring recording request
      request = StringUtils::Format("/service?method=recording.recurring.save&recurring_id=%d&event_id=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&only_new=%s",
        timerinfo.iClientIndex,
        epgOid,
        (int)timerinfo.iMaxRecordings,
        (int)timerinfo.iMarginStart,
        (int)timerinfo.iMarginEnd,
        days.c_str(),
        directory.c_str(),
        preventDuplicates
        );
    }
    break;

  case TIMER_REPEATING_MANUAL:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_MANUAL");
    // build manual recurring request
    request = StringUtils::Format("/service?method=recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s",
      timerinfo.iClientIndex,
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)timerinfo.endTime,
      (int)timerinfo.iMaxRecordings,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      days.c_str(),
      directory.c_str()
      );
    break;

  case TIMER_REPEATING_KEYWORD:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_KEYWORD");
    // build manual recurring request
    request = StringUtils::Format("/service?method=recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&keyword=%s&only_new=%s",
      timerinfo.iClientIndex,
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)timerinfo.endTime,
      (int)timerinfo.iMaxRecordings,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates
      );
    break;

  case TIMER_REPEATING_ADVANCED:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_ADVANCED");
    // build manual advanced recurring request
    request = StringUtils::Format("/service?method=recording.recurring.save&recurring_type=advanced&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&advanced=%s&only_new=%s",
      timerinfo.iClientIndex,
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)timerinfo.endTime,
      (int)timerinfo.iMaxRecordings,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates
      );
    break;
  }

  // send request to NextPVR
  std::string response;
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      if (timerinfo.startTime <= time(nullptr) && timerinfo.endTime > time(nullptr))
        PVR->TriggerRecordingUpdate();
      PVR->TriggerTimerUpdate();
      return PVR_ERROR_NO_ERROR;
    }
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::DeleteTimer(const PVR_TIMER& timer, bool bForceDelete)
{
  LOG_API_CALL(__FUNCTION__);
  std::string request = StringUtils::Format("/service?method=recording.delete&recording_id=%d", timer.iClientIndex);

  // handle recurring recordings
  if (timer.iTimerType >= TIMER_REPEATING_MIN && timer.iTimerType <= TIMER_REPEATING_MAX)
  {
    request = StringUtils::Format("/service?method=recording.recurring.delete&recurring_id=%d", timer.iClientIndex);
  }

  std::string response;
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      PVR->TriggerTimerUpdate();
      if (timer.startTime <= time(nullptr) && timer.endTime > time(nullptr))
        PVR->TriggerRecordingUpdate();
      return PVR_ERROR_NO_ERROR;
    }
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::UpdateTimer(const PVR_TIMER& timerinfo)
{
  LOG_API_CALL(__FUNCTION__);
  // not supported
  return AddTimer(timerinfo);
}

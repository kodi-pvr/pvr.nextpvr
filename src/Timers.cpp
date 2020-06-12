/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Timers.h"
#include "pvrclient-nextpvr.h"
#include "kodi/util/XMLUtils.h"
#include <kodi/General.h>
#include <p8-platform/util/StringUtils.h>
#include <string>

using namespace NextPVR;

/************************************************************/
/** Timer handling */

PVR_ERROR Timers::GetTimersAmount(int& amount)
{
  if (m_iTimerCount != -1)
  {
    amount = m_iTimerCount;
    return PVR_ERROR_NO_ERROR;
  }

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
    if (doc.Parse(response.c_str()))
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
    // to do why?
    m_iTimerCount = timerCount + 1;
  }
  amount = m_iTimerCount;
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Timers::GetTimers(kodi::addon::PVRTimersResultSet& results)
{
  std::string response;
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  int timerCount = 0;
  // first add the recurring recordings
  if (m_request.DoRequest("/service?method=recording.recurring.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()))
    {
      TiXmlElement* recurringsNode = doc.RootElement()->FirstChildElement("recurrings");
      TiXmlElement* pRecurringNode;
      for (pRecurringNode = recurringsNode->FirstChildElement("recurring"); pRecurringNode; pRecurringNode = pRecurringNode->NextSiblingElement())
      {
        kodi::addon::PVRTimer tag;
        TiXmlElement* pMatchRulesNode = pRecurringNode->FirstChildElement("matchrules");
        TiXmlElement* pRulesNode = pMatchRulesNode->FirstChildElement("Rules");

        tag.SetClientIndex(g_pvrclient->XmlGetUInt(pRecurringNode, "id"));
        tag.SetClientChannelUid(g_pvrclient->XmlGetInt(pRulesNode, "ChannelOID"));
        tag.SetTimerType(pRulesNode->FirstChildElement("EPGTitle") ? TIMER_REPEATING_EPG : TIMER_REPEATING_MANUAL);

        std::string buffer;

        // start/end time

        const int recordingType = g_pvrclient->XmlGetUInt(pRecurringNode, "type");

        if (recordingType == 1 || recordingType == 2)
        {
          tag.SetStartTime(0);
          tag.SetEndTime(0);
          tag.SetStartAnyTime(true);
          tag.SetEndAnyTime(true);
        }
        else
        {
          if (XMLUtils::GetString(pRulesNode, "StartTimeTicks", buffer))
            tag.SetStartTime(stoll(buffer));
          if (XMLUtils::GetString(pRulesNode, "EndTimeTicks", buffer))
            tag.SetEndTime(stoll(buffer));
        }

        // keyword recordings
        std::string advancedRulesText;
        if (XMLUtils::GetString(pRulesNode, "AdvancedRules", advancedRulesText))
        {
          if (advancedRulesText.find("KEYWORD: ") != std::string::npos)
          {
            tag.SetTimerType(TIMER_REPEATING_KEYWORD);
            tag.SetStartTime(0);
            tag.SetEndTime(0);
            tag.SetStartAnyTime(true);
            tag.SetEndAnyTime(true);
            tag.SetEPGSearchString(advancedRulesText.substr(9));
          }
          else
          {
            tag.SetTimerType(TIMER_REPEATING_ADVANCED);
            tag.SetStartTime(0);
            tag.SetEndTime(0);
            tag.SetStartAnyTime(true);
            tag.SetEndAnyTime(true);
            tag.SetFullTextEpgSearch(true);
            tag.SetEPGSearchString(advancedRulesText);
          }
        }

        // days
        tag.SetWeekdays(PVR_WEEKDAY_ALLDAYS);
        std::string daysText;
        if (XMLUtils::GetString(pRulesNode, "Days", daysText))
        {
          unsigned int weekdays = PVR_WEEKDAY_NONE;
          if (daysText.find("SUN") != std::string::npos)
            weekdays |= PVR_WEEKDAY_SUNDAY;
          if (daysText.find("MON") != std::string::npos)
            weekdays |= PVR_WEEKDAY_MONDAY;
          if (daysText.find("TUE") != std::string::npos)
            weekdays |= PVR_WEEKDAY_TUESDAY;
          if (daysText.find("WED") != std::string::npos)
            weekdays |= PVR_WEEKDAY_WEDNESDAY;
          if (daysText.find("THU") != std::string::npos)
            weekdays |= PVR_WEEKDAY_THURSDAY;
          if (daysText.find("FRI") != std::string::npos)
            weekdays |= PVR_WEEKDAY_FRIDAY;
          if (daysText.find("SAT") != std::string::npos)
            weekdays |= PVR_WEEKDAY_SATURDAY;
          tag.SetWeekdays(weekdays);
        }

        // pre/post padding
        tag.SetMarginStart(g_pvrclient->XmlGetUInt(pRulesNode, "PrePadding"));
        tag.SetMarginEnd(g_pvrclient->XmlGetUInt(pRulesNode, "PostPadding"));

        // number of recordings to keep
        tag.SetMaxRecordings(g_pvrclient->XmlGetInt(pRulesNode, "Keep"));

        // prevent duplicates
        bool duplicate;
        if (XMLUtils::GetBoolean(pRulesNode, "OnlyNewEpisodes", duplicate))
        {
          if (duplicate == true)
          {
            tag.SetPreventDuplicateEpisodes(1);
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
              tag.SetRecordingGroup(i);
              break;
            }
          }
        }

        buffer.clear();
        XMLUtils::GetString(pRecurringNode, "name", buffer);
        tag.SetTitle(buffer);
        bool state = true;
        XMLUtils::GetBoolean(pRulesNode, "enabled", state);
        if (state == false)
            tag.SetState(PVR_TIMER_STATE_DISABLED);
        else
            tag.SetState(PVR_TIMER_STATE_SCHEDULED);
        tag.SetSummary("summary");
        // pass timer to xbmc
        timerCount++;
        results.Add(tag);
      }
    }
    // next add the one-off recordings.
    response = "";
    if (m_request.DoRequest("/service?method=recording.list&filter=pending", response) == HTTP_OK)
    {
      TiXmlDocument doc;
      if (doc.Parse(response.c_str()) != nullptr)
      {
        TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
        for (TiXmlElement* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
        {
          kodi::addon::PVRTimer tag;
          UpdatePvrTimer(pRecordingNode, tag);
          // pass timer to xbmc
          timerCount++;
          results.Add(tag);
        }
      }
      response = "";
      if (m_request.DoRequest("/service?method=recording.list&filter=conflict", response) == HTTP_OK)
      {
        TiXmlDocument doc;
        if (doc.Parse(response.c_str()))
        {
          TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
          for (TiXmlElement* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
          {
            kodi::addon::PVRTimer tag;
            UpdatePvrTimer(pRecordingNode, tag);
            // pass timer to xbmc
            timerCount++;
            results.Add(tag);
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
  return returnValue;
}

bool Timers::UpdatePvrTimer(TiXmlElement* pRecordingNode, kodi::addon::PVRTimer& tag)
{
  tag.SetTimerType(pRecordingNode->FirstChildElement("epg_event_oid") ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL);
  tag.SetClientIndex(g_pvrclient->XmlGetUInt(pRecordingNode, "id"));
  tag.SetClientChannelUid(g_pvrclient->XmlGetUInt(pRecordingNode, "channel_id"));
  tag.SetParentClientIndex(g_pvrclient->XmlGetUInt(pRecordingNode, "recurring_parent", PVR_TIMER_NO_PARENT));

  if (tag.GetParentClientIndex() != PVR_TIMER_NO_PARENT)
  {
    if (tag.GetTimerType() == TIMER_ONCE_EPG)
      tag.SetTimerType(TIMER_ONCE_EPG_CHILD);
    else
      tag.SetTimerType(TIMER_ONCE_MANUAL_CHILD);
  }
  tag.SetEPGUid(g_pvrclient->XmlGetUInt(pRecordingNode, "epg_event_oid", PVR_TIMER_NO_EPG_UID));
  if (tag.GetEPGUid() != PVR_TIMER_NO_EPG_UID)
  {
    kodi::Log(ADDON_LOG_DEBUG, "Setting timer epg id %d %d", tag.GetClientIndex(), tag.GetEPGUid());
  }

  tag.SetMarginStart(g_pvrclient->XmlGetUInt(pRecordingNode, "pre_padding"));
  tag.SetMarginEnd(g_pvrclient->XmlGetUInt(pRecordingNode, "post_padding"));

  std::string buffer;

  // name
  XMLUtils::GetString(pRecordingNode, "name", buffer);
  tag.SetTitle(buffer);
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "desc", buffer);
  tag.SetSummary(buffer);
  // start/end time
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "start_time_ticks", buffer);
  buffer.resize(10);
  tag.SetStartTime(std::stoll(buffer));
  buffer.clear();
  XMLUtils::GetString(pRecordingNode, "duration_seconds", buffer);
  tag.SetEndTime(tag.GetStartTime() + std::stoll(buffer));

  tag.SetState(PVR_TIMER_STATE_SCHEDULED);

  std::string status;
  if (XMLUtils::GetString(pRecordingNode, "status", status))
  {
    if (status == "Recording" || (status == "Pending" && tag.GetStartTime() < time(nullptr) + m_settings.m_serverTimeOffset))
    {
      tag.SetState(PVR_TIMER_STATE_RECORDING);
    }
    else if (status == "Conflict")
    {
      tag.SetState(PVR_TIMER_STATE_CONFLICT_NOK);
    }
  }

  return true;
}

namespace
{
  struct TimerType : kodi::addon::PVRTimerType
  {
    TimerType(unsigned int id,
              unsigned int attributes,
              const std::string& description = std::string(),
              const std::vector<kodi::addon::PVRTypeIntValue>& maxRecordingsValues = std::vector<kodi::addon::PVRTypeIntValue>(),
              const int maxRecordingsDefault = 0,
              const std::vector<kodi::addon::PVRTypeIntValue>& dupEpisodesValues = std::vector<kodi::addon::PVRTypeIntValue>(),
              int dupEpisodesDefault = 0,
              const std::vector<kodi::addon::PVRTypeIntValue>& recordingGroupsValues =  std::vector<kodi::addon::PVRTypeIntValue>(),
              int recordingGroupDefault = 0,
              int option = 0)
              //const std::vector<kodi::addon::PVRTypeIntValue>& preventDuplicatesValues = std::vector<kodi::addon::PVRTypeIntValue>())
    {
      SetId(id);
      SetAttributes(attributes);
      SetMaxRecordings(maxRecordingsValues, maxRecordingsDefault);
      SetPreventDuplicateEpisodes(dupEpisodesValues, dupEpisodesDefault);
      SetRecordingGroups(recordingGroupsValues, recordingGroupDefault);
      SetDescription(description);
    }
  };

} // unnamed namespace

PVR_ERROR Timers::GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types)
{
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
  static std::vector<kodi::addon::PVRTypeIntValue> recordingLimitValues;
  if (recordingLimitValues.size() == 0)
  {
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_ASMANY, kodi::GetLocalizedString(MSG_KEEPALL)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_1, kodi::GetLocalizedString(MSG_KEEP1)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_2, kodi::GetLocalizedString(MSG_KEEP2)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_3, kodi::GetLocalizedString(MSG_KEEP3)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_4, kodi::GetLocalizedString(MSG_KEEP4)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_5, kodi::GetLocalizedString(MSG_KEEP5)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_6, kodi::GetLocalizedString(MSG_KEEP6)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_7, kodi::GetLocalizedString(MSG_KEEP7)));
    recordingLimitValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_LIMIT_10, kodi::GetLocalizedString(MSG_KEEP10)));
  }

  /* PVR_Timer.iPreventDuplicateEpisodes values and presentation.*/
  static std::vector<kodi::addon::PVRTypeIntValue> showTypeValues;
  if (showTypeValues.size() == 0)
  {
    showTypeValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_SHOWTYPE_FIRSTRUNONLY, kodi::GetLocalizedString(MSG_SHOWTYPE_FIRSTRUNONLY)));
    showTypeValues.emplace_back(kodi::addon::PVRTypeIntValue(NEXTPVR_SHOWTYPE_ANY, kodi::GetLocalizedString(MSG_SHOWTYPE_ANY)));
  }

  /* PVR_Timer.iRecordingGroup values and presentation */
  int i = 0;
  static std::vector<kodi::addon::PVRTypeIntValue> recordingGroupValues;
  for (auto it = m_settings.m_recordingDirectories.begin(); it != m_settings.m_recordingDirectories.end(); ++it, i++)
  {
    recordingGroupValues.emplace_back(kodi::addon::PVRTypeIntValue(i, m_settings.m_recordingDirectories[i]));
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
    TimerType* t = t = new TimerType(
      /* Type id. */
      TIMER_ONCE_EPG,
      /* Attributes. */
      TIMER_EPG_ATTRIBS,
      /* Description. */
      kodi::GetLocalizedString(MSG_ONETIME_GUIDE), // "One time (guide)",
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    t =new TimerType(
    /* One-shot manual (time and channel based) */
        /* Type id. */
        TIMER_ONCE_MANUAL,
        /* Attributes. */
        TIMER_MANUAL_ATTRIBS,
        /* Description. */
        kodi::GetLocalizedString(MSG_ONETIME_MANUAL), // "One time (manual)",
        /* Values definitions for attributes. */
        recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Repeating manual (time and channel based) Parent */
    t = new TimerType(
        /* Type id. */
        TIMER_REPEATING_MANUAL,
        /* Attributes. */
        TIMER_MANUAL_ATTRIBS | TIMER_REPEATING_MANUAL_ATTRIBS,
        /* Description. */
        kodi::GetLocalizedString(MSG_REPEATING_MANUAL), // "Repeating (manual)"
        /* Values definitions for attributes. */
        recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Repeating epg based Parent*/
    t = new TimerType(
        /* Type id. */
        TIMER_REPEATING_EPG,
        /* Attributes. */
        TIMER_EPG_ATTRIBS | TIMER_REPEATING_EPG_ATTRIBS,
        /* Description. */
        kodi::GetLocalizedString(MSG_REPEATING_GUIDE), // "Repeating (guide)"
        /* Values definitions for attributes. */
        recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Read-only one-shot for timers generated by timerec */
    t = new TimerType(
        /* Type id. */
        TIMER_ONCE_MANUAL_CHILD,
        /* Attributes. */
        TIMER_MANUAL_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
        /* Description. */
        kodi::GetLocalizedString(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
        /* Values definitions for attributes. */
        recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Read-only one-shot for timers generated by autorec */
    t = new TimerType(
        /* Type id. */
        TIMER_ONCE_EPG_CHILD,
        /* Attributes. */
        TIMER_EPG_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
        /* Description. */
        kodi::GetLocalizedString(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
        /* Values definitions for attributes. */
        recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    /* Repeating epg based Parent*/
    t = new TimerType(
        /* Type id. */
        TIMER_REPEATING_KEYWORD,
        /* Attributes. */
        TIMER_KEYWORD_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
        /* Description. */
        kodi::GetLocalizedString(MSG_REPEATING_KEYWORD), // "Repeating (keyword)"
        /* Values definitions for attributes. */
        recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

    t = new TimerType(
        /* Type id. */
        TIMER_REPEATING_ADVANCED,
        /* Attributes. */
        TIMER_ADVANCED_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
        /* Description. */
        kodi::GetLocalizedString(MSG_REPEATING_ADVANCED), // "Repeating (advanced)"
        /* Values definitions for attributes. */
        recordingLimitValues, m_defaultLimit, showTypeValues, m_defaultShowType, recordingGroupValues, 0);
    types.emplace_back(*t);
    delete t;

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
PVR_ERROR Timers::AddTimer(const kodi::addon::PVRTimer& timer)
{

  char preventDuplicates[16];
  if (timer.GetPreventDuplicateEpisodes() > 0)
    strcpy(preventDuplicates, "true");
  else
    strcpy(preventDuplicates, "false");

  const std::string encodedName = UriEncode(timer.GetTitle());
  const std::string encodedKeyword = UriEncode(timer.GetEPGSearchString());
  const std::string days = GetDayString(timer.GetWeekdays());
  const std::string directory = UriEncode(m_settings.m_recordingDirectories[timer.GetRecordingGroup()]);
  const std::string oidKey = std::to_string(timer.GetEPGUid()) + ":" + std::to_string(timer.GetClientChannelUid());
  const int epgOid = m_epgOidLookup[oidKey];
  kodi::Log(ADDON_LOG_DEBUG, "TIMER_%d %s", epgOid, oidKey.c_str());
  std::string request;
  switch (timer.GetTimerType())
  {
  case TIMER_ONCE_MANUAL:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_ONCE_MANUAL");
    // build one-off recording request
    request = StringUtils::Format("/service?method=recording.save&name=%s&channel=%d&time_t=%d&duration=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      (int)timer.GetStartTime(),
      (int)(timer.GetEndTime() - timer.GetStartTime()),
      timer.GetMarginStart(),
      timer.GetMarginEnd(),
      directory.c_str()
      );
    break;
  case TIMER_ONCE_EPG:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_ONCE_EPG");
    // build one-off recording request
    request = StringUtils::Format("/service?method=recording.save&recording_id=%d&event_id=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      timer.GetClientIndex(),
      epgOid,
      timer.GetMarginStart(),
      timer.GetMarginEnd(),
      directory.c_str());
    break;

  case TIMER_REPEATING_EPG:
    if (timer.GetClientChannelUid() == PVR_TIMER_ANY_CHANNEL)
    {
      // Fake a manual recording
      kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG ANY CHANNEL");
      std::string title = encodedName + "%";
      request = StringUtils::Format("/service?method=recording.recurring.save&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&keyword=%s",
        encodedName.c_str(),
        timer.GetClientChannelUid(),
        (int)timer.GetStartTime(),
        (int)timer.GetEndTime(),
        timer.GetMaxRecordings(),
        timer.GetMarginStart(),
        timer.GetMarginEnd(),
        days.c_str(),
        directory.c_str(),
        title.c_str()
        );
    }
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG");
      // build recurring recording request
      request = StringUtils::Format("/service?method=recording.recurring.save&recurring_id=%d&event_id=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&only_new=%s",
        timer.GetClientIndex(),
        epgOid,
        timer.GetMaxRecordings(),
        timer.GetMarginStart(),
        timer.GetMarginEnd(),
        days.c_str(),
        directory.c_str(),
        preventDuplicates
        );
    }
    break;

  case TIMER_REPEATING_MANUAL:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_MANUAL");
    // build manual recurring request
    request = StringUtils::Format("/service?method=recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      (int)timer.GetStartTime(),
      (int)timer.GetEndTime(),
      timer.GetMaxRecordings(),
      timer.GetMarginStart(),
      timer.GetMarginEnd(),
      days.c_str(),
      directory.c_str()
      );
    break;

  case TIMER_REPEATING_KEYWORD:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_KEYWORD");
    // build manual recurring request
    request = StringUtils::Format("/service?method=recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&keyword=%s&only_new=%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      (int)timer.GetStartTime(),
      (int)timer.GetEndTime(),
      timer.GetMaxRecordings(),
      timer.GetMarginStart(),
      timer.GetMarginEnd(),
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates
      );
    break;

  case TIMER_REPEATING_ADVANCED:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_ADVANCED");
    // build manual advanced recurring request
    request = StringUtils::Format("/service?method=recording.recurring.save&recurring_type=advanced&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&advanced=%s&only_new=%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      (int)timer.GetStartTime(),
      (int)timer.GetEndTime(),
      timer.GetMaxRecordings(),
      timer.GetMarginStart(),
      timer.GetMarginEnd(),
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
      if (timer.GetStartTime() <= time(nullptr) && timer.GetEndTime() > time(nullptr))
        g_pvrclient->TriggerRecordingUpdate();

      g_pvrclient->TriggerTimerUpdate();
      return PVR_ERROR_NO_ERROR;
    }
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  std::string request = "/service?method=recording.delete&recording_id=" + std::to_string(timer.GetClientIndex());

  // handle recurring recordings
  if (timer.GetTimerType() >= TIMER_REPEATING_MIN && timer.GetTimerType() <= TIMER_REPEATING_MAX)
  {
    request = "/service?method=recording.recurring.delete&recurring_id=" + std::to_string(timer.GetClientIndex());
  }

  std::string response;
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      g_pvrclient->TriggerTimerUpdate();
      if (timer.GetStartTime() <= time(nullptr) && timer.GetEndTime() > time(nullptr))
        g_pvrclient->TriggerRecordingUpdate();
      return PVR_ERROR_NO_ERROR;
    }
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  return AddTimer(timer);
}

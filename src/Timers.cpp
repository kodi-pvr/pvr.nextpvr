/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Timers.h"
#include "utilities/XMLUtils.h"

#include "pvrclient-nextpvr.h"
#include <kodi/General.h>
#include <kodi/tools/StringUtils.h>
#include <string>

using namespace NextPVR;
using namespace NextPVR::utilities;

/************************************************************/
/** Timer handling */

PVR_ERROR Timers::GetTimersAmount(int& amount)
{
  if (m_iTimerCount != -1)
  {
    amount = m_iTimerCount;
    return PVR_ERROR_NO_ERROR;
  }
  int timerCount = -1;
  // get list of recurring recordings
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("recording.recurring.list", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recurrings");
    if (recordingsNode != nullptr)
    {
      tinyxml2::XMLNode* pRecordingNode;
      for (pRecordingNode = recordingsNode->FirstChildElement("recurring"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        timerCount++;
      }
    }
  }
  // get list of pending recordings
  doc.Clear();
  if (m_request.DoMethodRequest("recording.list&filter=pending", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
    if (recordingsNode != nullptr)
    {
      tinyxml2::XMLNode* pRecordingNode;
      for (pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        timerCount++;
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
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  int timerCount = 0;
  // first add the recurring recordings
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("recording.recurring.list", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* recurringsNode = doc.RootElement()->FirstChildElement("recurrings");
    tinyxml2::XMLNode* pRecurringNode;
    for (pRecurringNode = recurringsNode->FirstChildElement("recurring"); pRecurringNode; pRecurringNode = pRecurringNode->NextSiblingElement())
    {
      kodi::addon::PVRTimer tag;
      tinyxml2::XMLNode* pMatchRulesNode = pRecurringNode->FirstChildElement("matchrules");
      tinyxml2::XMLNode* pRulesNode = pMatchRulesNode->FirstChildElement("Rules");

      tag.SetClientIndex(XMLUtils::GetUIntValue(pRecurringNode, "id"));
      int channelUID = XMLUtils::GetIntValue(pRulesNode, "ChannelOID");
      if (channelUID == 0)
      {
        tag.SetClientChannelUid(PVR_TIMER_ANY_CHANNEL);
      }
      else if (m_channels.m_channelDetails.find(channelUID) == m_channels.m_channelDetails.end())
      {
        kodi::Log(ADDON_LOG_DEBUG, "Invalid channel uid %d", channelUID);
        tag.SetClientChannelUid(PVR_CHANNEL_INVALID_UID);
      }
      else
      {
        tag.SetClientChannelUid(channelUID);
      }
      tag.SetTimerType(pRulesNode->FirstChildElement("EPGTitle") ? TIMER_REPEATING_EPG : TIMER_REPEATING_MANUAL);

      std::string buffer;

      // start/end time

      const int recordingType = XMLUtils::GetUIntValue(pRecurringNode, "type");

      if (recordingType == 1 || recordingType == 2)
      {
        tag.SetStartTime(TIMER_DATE_MIN);
        tag.SetEndTime(TIMER_DATE_MIN);
        tag.SetStartAnyTime(true);
        tag.SetEndAnyTime(true);
      }
      else
      {
        if (XMLUtils::GetString(pRulesNode, "StartTimeTicks", buffer))
          tag.SetStartTime(stoll(buffer));
        if (XMLUtils::GetString(pRulesNode, "EndTimeTicks", buffer))
          tag.SetEndTime(stoll(buffer));
        if (recordingType == 7)
        {
          tag.SetEPGSearchString(TYPE_7_TITLE);
        }
      }

      // keyword recordings
      std::string advancedRulesText;
      if (XMLUtils::GetString(pRulesNode, "AdvancedRules", advancedRulesText))
      {
        if (advancedRulesText.find("KEYWORD: ") != std::string::npos)
        {
          tag.SetTimerType(TIMER_REPEATING_KEYWORD);
          tag.SetStartTime(TIMER_DATE_MIN);
          tag.SetEndTime(TIMER_DATE_MIN);
          tag.SetStartAnyTime(true);
          tag.SetEndAnyTime(true);
          tag.SetEPGSearchString(advancedRulesText.substr(9));
        }
        else
        {
          tag.SetTimerType(TIMER_REPEATING_ADVANCED);
          tag.SetStartTime(TIMER_DATE_MIN);
          tag.SetEndTime(TIMER_DATE_MIN);
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
      tag.SetMarginStart(XMLUtils::GetUIntValue(pRulesNode, "PrePadding"));
      tag.SetMarginEnd(XMLUtils::GetUIntValue(pRulesNode, "PostPadding"));

      // number of recordings to keep
      tag.SetMaxRecordings(XMLUtils::GetIntValue(pRulesNode, "Keep"));

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
    // next add the one-off recordings.
    doc.Clear();
    if (m_request.DoMethodRequest("recording.list&filter=pending", doc) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      for (tinyxml2::XMLNode* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        kodi::addon::PVRTimer tag;
        UpdatePvrTimer(pRecordingNode, tag);
        // pass timer to xbmc
        timerCount++;
        results.Add(tag);
      }
    }
    doc.Clear();
    if (m_request.DoMethodRequest("recording.list&filter=conflict", doc) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      for (tinyxml2::XMLNode* pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode = pRecordingNode->NextSiblingElement())
      {
        kodi::addon::PVRTimer tag;
        UpdatePvrTimer(pRecordingNode, tag);
        // pass timer to xbmc
        timerCount++;
        results.Add(tag);
      }
      m_iTimerCount = timerCount;
    }
    m_lastTimerUpdateTime = time(nullptr);
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  return returnValue;
}

bool Timers::UpdatePvrTimer(tinyxml2::XMLNode* pRecordingNode, kodi::addon::PVRTimer& tag)
{
  tag.SetTimerType(pRecordingNode->FirstChildElement("epg_event_oid") ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL);
  tag.SetClientIndex(XMLUtils::GetUIntValue(pRecordingNode, "id"));
  tag.SetClientChannelUid(XMLUtils::GetUIntValue(pRecordingNode, "channel_id"));
  tag.SetParentClientIndex(XMLUtils::GetUIntValue(pRecordingNode, "recurring_parent", PVR_TIMER_NO_PARENT));

  if (tag.GetParentClientIndex() != PVR_TIMER_NO_PARENT)
  {
    if (tag.GetTimerType() == TIMER_ONCE_EPG)
      tag.SetTimerType(TIMER_ONCE_EPG_CHILD);
    else
      tag.SetTimerType(TIMER_ONCE_MANUAL_CHILD);
  }

  tag.SetMarginStart(XMLUtils::GetUIntValue(pRecordingNode, "pre_padding"));
  tag.SetMarginEnd(XMLUtils::GetUIntValue(pRecordingNode, "post_padding"));

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

  if (tag.GetTimerType() == TIMER_ONCE_EPG || tag.GetTimerType() == TIMER_ONCE_EPG_CHILD)
  {
    tag.SetEPGUid(XMLUtils::GetUIntValue(pRecordingNode, "epg_end_time_ticks", PVR_TIMER_NO_EPG_UID));

    // version 4 and some versions of v5 won't support the epg end time
    if (tag.GetEPGUid() == PVR_TIMER_NO_EPG_UID)
      tag.SetEPGUid(tag.GetEndTime());

    if (tag.GetEPGUid() != PVR_TIMER_NO_EPG_UID)
    {
      kodi::Log(ADDON_LOG_DEBUG, "Setting timer epg id %d %d", tag.GetClientIndex(), tag.GetEPGUid());
    }

  }

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

  int epgOid = 0;
  if (timer.GetEPGUid() > 0)
  {
    const std::string oidKey = std::to_string(timer.GetEPGUid()) + ":" + std::to_string(timer.GetClientChannelUid());
    epgOid = GetEPGOidForTimer(timer);
    kodi::Log(ADDON_LOG_DEBUG, "TIMER %d %s", epgOid, oidKey.c_str());
  }

  std::string request;

  int marginStart = timer.GetMarginStart();
  int marginEnd = timer.GetMarginEnd();
  if (m_settings.m_ignorePadding && timer.GetClientIndex() == PVR_TIMER_NO_CLIENT_INDEX && marginStart == 0 && marginStart == 0)
  {
    marginStart = m_settings.m_defaultPrePadding;
    marginEnd = m_settings.m_defaultPostPadding;
  }

  switch (timer.GetTimerType())
  {
  case TIMER_ONCE_MANUAL:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_ONCE_MANUAL");
    // build one-off recording request
    request = kodi::tools::StringUtils::Format("recording.save&name=%s&recording_id=%d&channel=%d&time_t=%d&duration=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      encodedName.c_str(),
      timer.GetClientIndex(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime() - timer.GetStartTime()),
      marginStart,
      marginEnd,
      directory.c_str()
      );
    break;
  case TIMER_ONCE_EPG:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_ONCE_EPG");
    // build one-off recording request
    request = kodi::tools::StringUtils::Format("recording.save&recording_id=%d&event_id=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      timer.GetClientIndex(),
      epgOid,
      marginStart,
      marginEnd,
      directory.c_str());
    break;

  case TIMER_REPEATING_EPG:
    if (timer.GetClientChannelUid() == PVR_TIMER_ANY_CHANNEL)
    // Fake a manual recording not a specific type in NextPVR
    {
      if (timer.GetEPGSearchString() == TYPE_7_TITLE)
      {
        kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG ANY CHANNEL - TYPE 7");
        request = kodi::tools::StringUtils::Format("recording.recurring.save&type=7&recurring_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s",
          timer.GetClientIndex(),
          static_cast<int>(timer.GetStartTime()),
          static_cast<int>(timer.GetEndTime()),
          timer.GetMaxRecordings(),
          marginStart,
          marginEnd,
          days.c_str(),
          directory.c_str()
          );
      }
      else
      {
        kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG ANY CHANNEL");
        std::string title = encodedName + "%";
        request = kodi::tools::StringUtils::Format("recording.recurring.save&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&keyword=%s",
          encodedName.c_str(),
          0,
          static_cast<int>(timer.GetStartTime()),
          static_cast<int>(timer.GetEndTime()),
          timer.GetMaxRecordings(),
          marginStart,
          marginEnd,
          days.c_str(),
          directory.c_str(),
          title.c_str()
          );
      }
    }
    else
    {
      kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_EPG");
      // build recurring recording request
      request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_id=%d&channel_id=%d&event_id=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&only_new=%s",
        timer.GetClientIndex(),
        timer.GetClientChannelUid(),
        epgOid,
        timer.GetMaxRecordings(),
        marginStart,
        marginEnd,
        days.c_str(),
        directory.c_str(),
        preventDuplicates
        );
    }
    break;

  case TIMER_REPEATING_MANUAL:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_MANUAL");
    // build manual recurring request
    request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime()),
      timer.GetMaxRecordings(),
      marginStart,
      marginEnd,
      days.c_str(),
      directory.c_str()
      );
    break;

  case TIMER_REPEATING_KEYWORD:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_KEYWORD");
    // build manual recurring request
    request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&keyword=%s&only_new=%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime()),
      timer.GetMaxRecordings(),
      marginStart,
      marginEnd,
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates
      );
    break;

  case TIMER_REPEATING_ADVANCED:
    kodi::Log(ADDON_LOG_DEBUG, "TIMER_REPEATING_ADVANCED");
    // build manual advanced recurring request
    request = kodi::tools::StringUtils::Format("recording.recurring.save&recurring_type=advanced&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&advanced=%s&only_new=%s",
      timer.GetClientIndex(),
      encodedName.c_str(),
      timer.GetClientChannelUid(),
      static_cast<int>(timer.GetStartTime()),
      static_cast<int>(timer.GetEndTime()),
      timer.GetMaxRecordings(),
      marginStart,
      marginEnd,
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates
      );
    break;
  }

  // send request to NextPVR
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    if (timer.GetStartTime() <= time(nullptr) && timer.GetEndTime() > time(nullptr))
      g_pvrclient->TriggerRecordingUpdate();

    g_pvrclient->TriggerTimerUpdate();
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete)
{
  std::string request = "recording.delete&recording_id=" + std::to_string(timer.GetClientIndex());

  // handle recurring recordings
  if (timer.GetTimerType() >= TIMER_REPEATING_MIN && timer.GetTimerType() <= TIMER_REPEATING_MAX)
  {
    request = "recording.recurring.delete&recurring_id=" + std::to_string(timer.GetClientIndex());
  }

  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    g_pvrclient->TriggerTimerUpdate();
    if (timer.GetStartTime() <= time(nullptr) && timer.GetEndTime() > time(nullptr))
      g_pvrclient->TriggerRecordingUpdate();
    return PVR_ERROR_NO_ERROR;
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR Timers::UpdateTimer(const kodi::addon::PVRTimer& timer)
{
  return AddTimer(timer);
}

int Timers::GetEPGOidForTimer(const kodi::addon::PVRTimer& timer)
{
  std::string request = kodi::tools::StringUtils::Format("channel.listings&channel_id=%d&start=%d&end=%d",
    timer.GetClientChannelUid(),timer.GetEPGUid() - 1, timer.GetEPGUid());

  tinyxml2::XMLDocument doc;
  int epgOid = 0;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* listingsNode = doc.RootElement()->FirstChildElement("listings");
    for (tinyxml2::XMLNode* pListingNode = listingsNode->FirstChildElement("l"); pListingNode; pListingNode = pListingNode->NextSiblingElement())
    {
      std::string endTime;
      XMLUtils::GetString(pListingNode, "end", endTime);
      endTime.resize(10);
      if (atoi(endTime.c_str()) == timer.GetEPGUid())
      {
        epgOid = XMLUtils::GetIntValue(pListingNode, "id");
        break;
      }
    }
  }
  return epgOid;
}

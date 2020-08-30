/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "BackendRequest.h"
#include <kodi/addon-instance/PVR.h>
#include <algorithm>



namespace NextPVR
{
  /* timer type ids */
  constexpr unsigned int TIMER_MANUAL_MIN = PVR_TIMER_TYPE_NONE + 1;
  constexpr unsigned int TIMER_ONCE_MANUAL = TIMER_MANUAL_MIN;
  constexpr unsigned int TIMER_ONCE_EPG = TIMER_MANUAL_MIN + 1;
  constexpr unsigned int TIMER_ONCE_KEYWORD = TIMER_MANUAL_MIN + 2;
  constexpr unsigned int TIMER_ONCE_MANUAL_CHILD = TIMER_MANUAL_MIN + 3;
  constexpr unsigned int TIMER_ONCE_EPG_CHILD = TIMER_MANUAL_MIN + 4;
  constexpr unsigned int TIMER_ONCE_KEYWORD_CHILD =TIMER_MANUAL_MIN + 5;
  constexpr unsigned int TIMER_MANUAL_MAX = TIMER_MANUAL_MIN + 5;

  constexpr unsigned int TIMER_REPEATING_MIN = TIMER_MANUAL_MAX + 1;
  constexpr unsigned int TIMER_REPEATING_MANUAL = TIMER_REPEATING_MIN;
  constexpr unsigned int TIMER_REPEATING_EPG = TIMER_REPEATING_MIN + 1;
  constexpr unsigned int TIMER_REPEATING_KEYWORD = TIMER_REPEATING_MIN + 2;
  constexpr unsigned int TIMER_REPEATING_ADVANCED = TIMER_REPEATING_MIN + 3;
  constexpr unsigned int TIMER_REPEATING_MAX = TIMER_REPEATING_MIN + 3;

  class Timers
  {
    typedef enum
    {
      NEXTPVR_SHOWTYPE_ANY = 0,
      NEXTPVR_SHOWTYPE_FIRSTRUNONLY = 1,
    } nextpvr_showtype_t;

    typedef enum
    {
      NEXTPVR_LIMIT_ASMANY = 0,
      NEXTPVR_LIMIT_1 = 1,
      NEXTPVR_LIMIT_2 = 2,
      NEXTPVR_LIMIT_3 = 3,
      NEXTPVR_LIMIT_4 = 4,
      NEXTPVR_LIMIT_5 = 5,
      NEXTPVR_LIMIT_6 = 6,
      NEXTPVR_LIMIT_7 = 7,
      NEXTPVR_LIMIT_10 = 10
    } nextpvr_recordinglimit_t;

  public:
    //Timers(kodi::addon::CInstancePVRClient& instance) : m_instance(instance) {};
    /**
       * Singleton getter for the instance
    */
    static Timers& GetInstance()
    {
      static Timers timers;
      return timers;
    }


    /* Timer handling */
    PVR_ERROR GetTimersAmount(int& amount);
    PVR_ERROR GetTimerTypes(std::vector<kodi::addon::PVRTimerType>& types);
    PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results);
    PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer);
    PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete);
    PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer);
    bool UpdatePvrTimer(tinyxml2::XMLNode* pRecordingNode, kodi::addon::PVRTimer& tag);
    std::map<std::string, int> m_epgOidLookup;
    time_t m_lastTimerUpdateTime = 0;

  private:
    Timers() = default;

    Timers(Timers const&) = delete;
    void operator=(Timers const&) = delete;

    Settings& m_settings = Settings::GetInstance();
    Request& m_request = Request::GetInstance();
    //kodi::addon::CInstancePVRClient& m_instance;

    int m_defaultLimit = NEXTPVR_LIMIT_ASMANY;
    int m_defaultShowType = NEXTPVR_SHOWTYPE_ANY;
    int m_iTimerCount = -1;

    std::string GetDayString(int dayMask);
  };
} // namespace NextPVR

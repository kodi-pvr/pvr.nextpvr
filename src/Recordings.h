/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "BackendRequest.h"
#include "Timers.h"
#include <kodi/addon-instance/PVR.h>



namespace NextPVR
{

  class ATTRIBUTE_HIDDEN Recordings
  {

  public:
    /**
       * Singleton getter for the instance
       */
    static Recordings& GetInstance()
    {
      static Recordings recordings;
      return recordings;
    }

    /* Recording handling **/
    PVR_ERROR GetRecordingsAmount(bool deleted, int& amount);
    PVR_ERROR GetDriveSpace(uint64_t& total, uint64_t& used);
    PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results);
    PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording);
    PVR_ERROR SetRecordingPlayCount(const kodi::addon::PVRRecording& recording, int count);
    PVR_ERROR SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int lastplayedposition);
    PVR_ERROR GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position);
    PVR_ERROR GetRecordingsLastPlayedPosition();
    PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl);
    PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*);
    bool UpdatePvrRecording(const tinyxml2::XMLNode* pRecordingNode, kodi::addon::PVRRecording& tag, const std::string& title, bool flatten, bool multipleSeasons);
    bool ParseNextPVRSubtitle(const tinyxml2::XMLNode*, kodi::addon::PVRRecording& tag);
    bool ForgetRecording(const kodi::addon::PVRRecording& recording);
    std::map<std::string, std::string> m_hostFilenames;

  private:
    Recordings() = default;

    Recordings(Recordings const&) = delete;
    void operator=(Recordings const&) = delete;

    Settings& m_settings = Settings::GetInstance();
    Request& m_request = Request::GetInstance();
    Timers& m_timers = Timers::GetInstance();

    // update these at end of counting loop can be called during action
    int m_iRecordingCount = -1;
    std::map<int, int> m_lastPlayed;
    std::map<int, int> m_playCount;

    time_t m_checkedSpace = std::numeric_limits<uint64_t>::max();
    mutable std::mutex m_mutexSpace;
    uint64_t m_total = 0;
    uint64_t m_used = 0;

  };
} // namespace NextPVR

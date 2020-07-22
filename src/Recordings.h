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
#include "tinyxml.h"



namespace NextPVR
{

  class Recordings
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
    PVR_ERROR GetRecordings(bool deleted, kodi::addon::PVRRecordingsResultSet& results);
    PVR_ERROR DeleteRecording(const kodi::addon::PVRRecording& recording);
    PVR_ERROR SetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int lastplayedposition);
    PVR_ERROR GetRecordingLastPlayedPosition(const kodi::addon::PVRRecording& recording, int& position);
    PVR_ERROR GetRecordingEdl(const kodi::addon::PVRRecording& recording, std::vector<kodi::addon::PVREDLEntry>& edl);
    PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*);
    bool UpdatePvrRecording(TiXmlElement* pRecordingNode, kodi::addon::PVRRecording& tag, const std::string& title, bool flatten, bool multipleSeasons);
    bool ParseNextPVRSubtitle(TiXmlElement* pRecordingNode, kodi::addon::PVRRecording& tag);
    bool ForgetRecording(const kodi::addon::PVRRecording& recording);
    std::map<std::string, std::string> m_hostFilenames;
    bool GetAdditiveString(const TiXmlNode* pRootNode, const char* strTag, const std::string& strSeparator, std::string& strStringValue, bool clear);


  private:
    Recordings() = default;

    Recordings(Recordings const&) = delete;
    void operator=(Recordings const&) = delete;

    Settings& m_settings = Settings::GetInstance();
    Request& m_request = Request::GetInstance();
    // update these at end of counting loop can be called during action
    int m_iRecordingCount = -1;

  };
} // namespace NextPVR

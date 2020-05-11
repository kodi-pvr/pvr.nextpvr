/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include "BackendRequest.h"
#include "tinyxml.h"

using namespace ADDON;

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
    int GetNumRecordings(void);
    PVR_ERROR GetRecordings(ADDON_HANDLE handle);
    PVR_ERROR DeleteRecording(const PVR_RECORDING& recording);
    PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING& recording, int lastplayedposition);
    int GetRecordingLastPlayedPosition(const PVR_RECORDING& recording);
    PVR_ERROR GetRecordingEdl(const PVR_RECORDING& recording, PVR_EDL_ENTRY[], int* size);
    PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*);
    bool UpdatePvrRecording(TiXmlElement* pRecordingNode, PVR_RECORDING* tag, const std::string& title, bool flatten);
    void ParseNextPVRSubtitle(const std::string episodeName, PVR_RECORDING* tag);
    bool ForgetRecording(const PVR_RECORDING& recording);
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

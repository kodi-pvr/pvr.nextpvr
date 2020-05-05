/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <vector>

/* Master defines for client control */
#include "kodi/xbmc_pvr_types.h"
#include "kodi/libXBMC_addon.h"

/* Local includes */
#include "Settings.h"
#include "buffers/DummyBuffer.h"
#include "buffers/TranscodedBuffer.h"
#include "buffers/TimeshiftBuffer.h"
#include "buffers/RecordingBuffer.h"
#include "buffers/RollingFile.h"
#include "buffers/ClientTimeshift.h"

#include "p8-platform/threads/mutex.h"
#include "p8-platform/threads/threads.h"
#include "tinyxml.h"
#include <map>

#define SAFE_DELETE(p)       do { delete (p);     (p)=NULL; } while (0)

#define PVR_MENUHOOK_CHANNEL_DELETE_SINGLE_CHANNEL_ICON 101
#define PVR_MENUHOOK_RECORDING_FORGET_RECORDING 401
#define PVR_MENUHOOK_SETTING_DELETE_ALL_CHANNNEL_ICONS 601
#define PVR_MENUHOOK_SETTING_UPDATE_CHANNNELS 602
#define PVR_MENUHOOK_SETTING_UPDATE_CHANNNEL_GROUPS 603

/* timer type ids */
#define TIMER_MANUAL_MIN          (PVR_TIMER_TYPE_NONE + 1)
#define TIMER_ONCE_MANUAL         (TIMER_MANUAL_MIN + 0)
#define TIMER_ONCE_EPG            (TIMER_MANUAL_MIN + 1)
#define TIMER_ONCE_KEYWORD        (TIMER_MANUAL_MIN + 2)
#define TIMER_ONCE_MANUAL_CHILD   (TIMER_MANUAL_MIN + 3)
#define TIMER_ONCE_EPG_CHILD      (TIMER_MANUAL_MIN + 4)
#define TIMER_ONCE_KEYWORD_CHILD  (TIMER_MANUAL_MIN + 5)
#define TIMER_MANUAL_MAX          (TIMER_MANUAL_MIN + 5)

#define TIMER_REPEATING_MIN       (TIMER_MANUAL_MAX + 1)
#define TIMER_REPEATING_MANUAL    (TIMER_REPEATING_MIN + 0)
#define TIMER_REPEATING_EPG       (TIMER_REPEATING_MIN + 1)
#define TIMER_REPEATING_KEYWORD   (TIMER_REPEATING_MIN + 2)
#define TIMER_REPEATING_ADVANCED  (TIMER_REPEATING_MIN + 3)
#define TIMER_REPEATING_MAX       (TIMER_REPEATING_MIN + 3)

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

enum eNowPlaying
{
  NotPlaying = 0,
  TV = 1,
  Radio = 2,
  Recording = 3,
  Transcoding
};

class cPVRClientNextPVR : P8PLATFORM::CThread
{
public:
  /* Class interface */
  cPVRClientNextPVR();
  ~cPVRClientNextPVR();

  /* Server handling */
  ADDON_STATUS Connect();
  void Disconnect();
  bool IsUp();
  void OnSystemSleep();
  void OnSystemWake();
  void LoadLiveStreams();

  /* General handling */
  const char* GetBackendName(void);
  const char* GetBackendVersion();
  const char* GetConnectionString(void);
  PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed);
  PVR_ERROR GetBackendTime(time_t *localTime, int *gmtOffset);
  PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *stimes);
  PVR_ERROR GetStreamReadChunkSize(int* chunksize);
  int XmlGetInt(TiXmlElement * node, const char* name);
  PVR_ERROR CallMenuHook(const PVR_MENUHOOK& menuhook, const PVR_MENUHOOK_DATA& item);
  void ConfigureMenuhook();

  /* EPG handling */
  PVR_ERROR GetEpg(ADDON_HANDLE handle, int iChannelUid, time_t iStart = 0, time_t iEnd = 0);

  /* Channel handling */
  int GetNumChannels(void);
  PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio);
  PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL &channel, PVR_NAMED_VALUE *properties, unsigned int *iPropertiesCount);
  bool IsChannelAPlugin(int uid);

  /* Channel group handling */
  int GetChannelGroupsAmount(void);
  PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio);
  PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group);

  /* Record handling **/
  int GetNumRecordings(void);
  PVR_ERROR GetRecordings(ADDON_HANDLE handle);
  PVR_ERROR DeleteRecording(const PVR_RECORDING &recording);
  PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition);
  int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording);
  PVR_ERROR GetRecordingEdl(const PVR_RECORDING& recording, PVR_EDL_ENTRY[], int *size);
  PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*);
  bool UpdatePvrRecording(TiXmlElement* pRecordingNode, PVR_RECORDING *tag, const std::string& title, bool flatten);
  void ParseNextPVRSubtitle( const char *episodeName, PVR_RECORDING   *tag);
  bool ForgetRecording(const PVR_RECORDING& recording);

  /* Timer handling */
  int GetNumTimers(void);
  PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size);
  PVR_ERROR GetTimers(ADDON_HANDLE handle);
  PVR_ERROR GetTimerInfo(unsigned int timernumber, PVR_TIMER &timer);
  PVR_ERROR AddTimer(const PVR_TIMER &timer);
  PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete = false);
  PVR_ERROR UpdateTimer(const PVR_TIMER &timer);
  bool UpdatePvrTimer(TiXmlElement* pRecordingNode, PVR_TIMER *tag);

  /* Live stream handling */
  bool OpenLiveStream(const PVR_CHANNEL &channel);
  void CloseLiveStream();
  int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize);
  PVR_ERROR GetSignalStatus(PVR_SIGNAL_STATUS *signalStatus);
  long long SeekLiveStream(long long iPosition, int iWhence = SEEK_SET);
  long long LengthLiveStream(void);
  bool CanPauseStream(void);
  void PauseStream(bool bPause);
  bool CanSeekStream(void);
  bool IsTimeshifting(void);
  bool IsRealTimeStream(void);

  /* Record stream handling */
  bool OpenRecordedStream(const PVR_RECORDING &recording);
  void CloseRecordedStream(void);
  int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize);
  long long SeekRecordedStream(long long iPosition, int iWhence = SEEK_SET);
  long long LengthRecordedStream(void);
  void ForceRecordingUpdate() { m_lastRecordingUpdateTime = 0;}

  /* background connection monitoring */
  void *Process(void);

protected:
  NextPVR::Socket           *m_tcpclient;
  NextPVR::Socket           *m_streamingclient;

private:
  std::string GetDayString(int dayMask);
  std::vector<std::string> split(const std::string& s, const std::string& delim, const bool keep_empty);
  bool GetChannel(unsigned int number, PVR_CHANNEL &channeldata);
  bool LoadGenreXML(const std::string &filename);
  int DoRequest(const char *resource, std::string &response);
  std::string GetChannelIcon(int channelID);
  std::string GetChannelIconFileName(int channelID);
  void DeleteChannelIcon(int channelID);
  void DeleteChannelIcons();
  void ConfigurePostConnectionOptions();

  void Close();

  int                     m_iCurrentChannel;
  bool                    m_bConnected;
  std::string             m_BackendName;
  P8PLATFORM::CMutex      m_mutex;

  long long               m_currentRecordingLength;
  long long               m_currentRecordingPosition;

  bool                    m_supportsLiveTimeshift;
  long long               m_currentLiveLength;
  long long               m_currentLivePosition;

  int64_t                 m_lastRecordingUpdateTime;

  char                    m_sid[64];

  // update these at end of counting loop can be called during action
  int                     m_iRecordingCount = -1;
  int                     m_iTimerCount = -1;

  int                     m_defaultLimit;
  int                     m_defaultShowType;
  time_t                  m_tsbStartTime;
  int                     m_timeShiftBufferSeconds;
  timeshift::Buffer      *m_timeshiftBuffer;
  timeshift::Buffer      *m_livePlayer;
  timeshift::Buffer      *m_realTimeBuffer;
  timeshift::RecordingBuffer *m_recordingBuffer;

  std::map<std::string, std::string> m_hostFilenames;
  std::map<int, std::string> m_liveStreams;

  //Matrix changes
  NextPVR::Settings& m_settings = NextPVR::Settings::GetInstance();
  NextPVR::Request& m_request = NextPVR::Request::GetInstance();
  std::map<std::string,int> m_epgOidLookup;
  eNowPlaying m_nowPlaying = NotPlaying;
  std::map<int, std::pair<bool, bool>> m_channelDetails;

  void SendWakeOnLan();

  PVR_RECORDING_CHANNEL_TYPE GetChannelType(unsigned int uid);

  bool GetAdditiveString(const TiXmlNode* pRootNode, const char* strTag,
        const std::string& strSeparator, std::string& strStringValue,
        bool clear);

};

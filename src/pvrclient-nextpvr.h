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
#include "kodi/libXBMC_addon.h"
#include "kodi/xbmc_pvr_types.h"

/* Local includes */

#include "Channels.h"
#include "EPG.h"
#include "MenuHook.h"
#include "Recordings.h"
#include "Settings.h"
#include "Timers.h"
#include "buffers/ClientTimeshift.h"
#include "buffers/DummyBuffer.h"
#include "buffers/RecordingBuffer.h"
#include "buffers/RollingFile.h"
#include "buffers/TimeshiftBuffer.h"
#include "buffers/TranscodedBuffer.h"
#include "p8-platform/threads/mutex.h"
#include "p8-platform/threads/threads.h"
#include "tinyxml.h"
#include <map>

#define SAFE_DELETE(p) \
  do \
  { \
    delete (p); \
    (p) = nullptr; \
  } while (0)
#define DEBUGGING_XML 0

#define DEBUGGING_API 0
#if DEBUGGING_API
#define LOG_API_CALL(f) XBMC->Log(LOG_INFO, "%s:  called!", f)
#define LOG_API_IRET(f, i) XBMC->Log(LOG_INFO, "%s: returns %d", f, i)
#else
#define LOG_API_CALL(f)
#define LOG_API_IRET(f, i)
#endif


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

  /* General handling */
  const char* GetBackendName(void);
  const char* GetBackendVersion();
  const char* GetConnectionString(void);
  PVR_ERROR GetDriveSpace(long long* iTotal, long long* iUsed);
  PVR_ERROR GetBackendTime(time_t* localTime, int* gmtOffset);
  PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES* stimes);
  PVR_ERROR GetStreamReadChunkSize(int* chunksize);
  int XmlGetInt(TiXmlElement* node, const char* name, const int setDefault = 0);
  unsigned int XmlGetUInt(TiXmlElement* node, const char* name, const unsigned setDefault = 0);

  PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL& channel, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount);
  bool IsChannelAPlugin(int uid);

  /* Live stream handling */
  bool OpenLiveStream(const PVR_CHANNEL& channel);
  void CloseLiveStream();
  int ReadLiveStream(unsigned char* pBuffer, unsigned int iBufferSize);
  PVR_ERROR GetSignalStatus(PVR_SIGNAL_STATUS* signalStatus);
  long long SeekLiveStream(long long iPosition, int iWhence = SEEK_SET);
  long long LengthLiveStream(void);
  bool CanPauseStream(void);
  void PauseStream(bool bPause);
  bool CanSeekStream(void);
  bool IsTimeshifting(void);
  bool IsRealTimeStream(void);

  /* Record stream handling */
  bool OpenRecordedStream(const PVR_RECORDING& recording);
  void CloseRecordedStream(void);
  int ReadRecordedStream(unsigned char* pBuffer, unsigned int iBufferSize);
  long long SeekRecordedStream(long long iPosition, int iWhence = SEEK_SET);
  long long LengthRecordedStream(void);
  void ForceRecordingUpdate() { m_lastRecordingUpdateTime = 0; }

  /* background connection monitoring */
  void* Process(void);

  Channels& m_channels = Channels::GetInstance();
  EPG& m_epg = EPG::GetInstance();
  MenuHook& m_menuhook = MenuHook::GetInstance();
  Recordings& m_recordings = Recordings::GetInstance();
  Timers& m_timers = Timers::GetInstance();
  int64_t m_lastRecordingUpdateTime;
  char m_sid[64];

protected:

private:
  bool GetChannel(unsigned int number, PVR_CHANNEL& channeldata);
  bool LoadGenreXML(const std::string& filename);

  std::string GetChannelIcon(int channelID);
  std::string GetChannelIconFileName(int channelID);
  void DeleteChannelIcon(int channelID);
  void DeleteChannelIcons();
  void ConfigurePostConnectionOptions();

  void Close();

  bool m_bConnected;
  std::string m_BackendName;
  P8PLATFORM::CMutex m_mutex;

  bool m_supportsLiveTimeshift;

  time_t m_tsbStartTime;
  int m_timeShiftBufferSeconds;
  timeshift::Buffer* m_timeshiftBuffer;
  timeshift::Buffer* m_livePlayer;
  timeshift::Buffer* m_realTimeBuffer;
  timeshift::RecordingBuffer* m_recordingBuffer;

  //Matrix changes
  NextPVR::Settings& m_settings = NextPVR::Settings::GetInstance();
  NextPVR::Request& m_request = NextPVR::Request::GetInstance();

  eNowPlaying m_nowPlaying = NotPlaying;

  void SendWakeOnLan();

  PVR_RECORDING_CHANNEL_TYPE GetChannelType(unsigned int uid);

};

#pragma once
/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <vector>

/* Master defines for client control */
#include "xbmc_pvr_types.h"

/* Local includes */
#include "Socket.h"
#include "p8-platform/threads/mutex.h"
#include "p8-platform/threads/threads.h"
#include "RingBuffer.h"
#include "liveshift.h"

#define SAFE_DELETE(p)       do { delete (p);     (p)=NULL; } while (0)


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
#define TIMER_REPEATING_MAX       (TIMER_REPEATING_MIN + 2)

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

class cPVRClientNextPVR : P8PLATFORM::CThread
{
public:
  /* Class interface */
  cPVRClientNextPVR();
  ~cPVRClientNextPVR();

  /* Server handling */
  bool Connect();
  void Disconnect();
  bool IsUp();

  /* General handling */
  const char* GetBackendName(void);
  const char* GetBackendVersion(void);
  const char* GetConnectionString(void);
  PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed);
  PVR_ERROR GetBackendTime(time_t *localTime, int *gmtOffset);

  /* EPG handling */
  PVR_ERROR GetEpg(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart = 0, time_t iEnd = 0);

  /* Channel handling */
  int GetNumChannels(void);
  PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio);  

  /* Channel group handling */
  int GetChannelGroupsAmount(void);
  PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio);
  PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group);

  /* Record handling **/
  int GetNumRecordings(void);
  PVR_ERROR GetRecordings(ADDON_HANDLE handle);
  PVR_ERROR DeleteRecording(const PVR_RECORDING &recording);
  PVR_ERROR RenameRecording(const PVR_RECORDING &recording);
  PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition);
  int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording);
  PVR_ERROR GetRecordingEdl(const PVR_RECORDING& recording, PVR_EDL_ENTRY[], int *size);

  /* Timer handling */
  int GetNumTimers(void);
  PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size);
  PVR_ERROR GetTimers(ADDON_HANDLE handle);
  PVR_ERROR GetTimerInfo(unsigned int timernumber, PVR_TIMER &timer);
  PVR_ERROR AddTimer(const PVR_TIMER &timer);
  PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete = false);
  PVR_ERROR UpdateTimer(const PVR_TIMER &timer);

  /* Live stream handling */
  bool OpenLiveStream(const PVR_CHANNEL &channel);
  void CloseLiveStream();
  int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize);
  bool SwitchChannel(const PVR_CHANNEL &channel);
  PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus);
  const char* GetLiveStreamURL(const PVR_CHANNEL &channel);
  long long SeekLiveStream(long long iPosition, int iWhence = SEEK_SET);
  long long LengthLiveStream(void);
  long long PositionLiveStream(void);
  bool CanPauseStream(void);
  bool CanSeekStream(void);

  /* Record stream handling */
  bool OpenRecordedStream(const PVR_RECORDING &recording);
  void CloseRecordedStream(void);
  int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize);
  long long SeekRecordedStream(long long iPosition, int iWhence = SEEK_SET);
  long long LengthRecordedStream(void);
  long long PositionRecordedStream(void);

  /* background connection monitoring */
  void *Process(void);

protected:
  NextPVR::Socket           *m_tcpclient;
  NextPVR::Socket           *m_streamingclient;

private:
  CStdString GetDayString(int dayMask);
  std::vector<CStdString> split(const CStdString& s, const CStdString& delim, const bool keep_empty);
  bool GetChannel(unsigned int number, PVR_CHANNEL &channeldata);
  bool LoadGenreXML(const std::string &filename);
  int DoRequest(const char *resource, CStdString &response);
  bool OpenRecordingInternal(long long seekOffset);
  CStdString GetChannelIcon(int channelID);
  void Close();

  int                     m_iCurrentChannel;
  bool                    m_bConnected;  
  std::string             m_BackendName;
  P8PLATFORM::CMutex        m_mutex;

  CRingBuffer             m_incomingStreamBuffer;

  char                    m_currentRecordingID[1024];
  long long               m_currentRecordingLength;
  long long               m_currentRecordingPosition;

  bool                    m_supportsLiveTimeshift;
  long long               m_currentLiveLength;
  long long               m_currentLivePosition;
  int                     m_iDefaultPrePadding;
  int                     m_iDefaultPostPadding;  
  std::vector< std::string > m_recordingDirectories;

  CStdString              m_PlaybackURL;
  LiveShiftSource        *m_pLiveShiftSource;

  int64_t                 m_lastRecordingUpdateTime;

  char                    m_sid[64];

  int                     m_iChannelCount;  

  int                     m_defaultLimit;
  int                     m_defaultShowType;
};

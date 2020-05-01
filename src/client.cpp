/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "client.h"
#include "kodi/xbmc_pvr_dll.h"
#include "pvrclient-nextpvr.h"

using namespace ADDON;
using namespace NextPVR;

#define PVR_MIN_API_VERSION "1.2.0"

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */

/* Client member variables */
ADDON_STATUS           m_CurStatus    = ADDON_STATUS_UNKNOWN;
cPVRClientNextPVR     *g_client       = nullptr;
std::string            g_szUserPath   = "";
std::string            g_szClientPath = "";

Settings& settings = Settings::GetInstance();

CHelper_libXBMC_addon *XBMC           = NULL;
CHelper_libXBMC_pvr   *PVR            = NULL;

extern "C" {

void ADDON_ReadSettings();

/***********************************************************
 * Standard AddOn related public library functions
 ***********************************************************/

//-- Create -------------------------------------------------------------------
// Called after loading of the dll, all steps to become Client functional
// must be performed here.
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
    return ADDON_STATUS_UNKNOWN;

  AddonProperties_PVR* pvrprops = (AddonProperties_PVR*)props;

  XBMC = new CHelper_libXBMC_addon;
  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_INFO, "Creating NextPVR PVR-Client");

  m_CurStatus    = ADDON_STATUS_UNKNOWN;
  g_szUserPath   = pvrprops->strUserPath;
  g_szClientPath = pvrprops->strClientPath;

  if (!XBMC->DirectoryExists("special://userdata/addon_data/pvr.nextpvr/"))
  {
    NextPVR::m_backEnd = new NextPVR::Request();
    NextPVR::m_backEnd->OneTimeSetup(hdl);
    NextPVR::m_backEnd->~Request();
  }

  settings.ReadFromAddon();

  /* Create connection to NextPVR KODI TV client */
  g_client       = new cPVRClientNextPVR();
  m_CurStatus = g_client->Connect();

  if (m_CurStatus != ADDON_STATUS_OK)
  {
    SAFE_DELETE(g_client);
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
  }

  return m_CurStatus;
}

//-- Destroy ------------------------------------------------------------------
// Used during destruction of the client, all steps to do clean and safe Create
// again must be done.
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  SAFE_DELETE(g_client);
  SAFE_DELETE(PVR);
  SAFE_DELETE(XBMC);

  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

//-- GetStatus ----------------------------------------------------------------
// Report the current Add-On Status to XBMC
// Note currently not called but needed to load
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  /* check whether we're still connected */
  if (m_CurStatus == ADDON_STATUS_OK && g_client && !g_client->IsUp())
    m_CurStatus = ADDON_STATUS_LOST_CONNECTION;

  return m_CurStatus;
}

void ADDON_ReadSettings(void)
{
  if (!XBMC)
    return;
}

//-- SetSetting ---------------------------------------------------------------
// Called everytime a setting is changed by the user and to inform AddOn about
// new setting and to do required stuff to apply it.
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  std::string str = settingName;

  // SetSetting can occur when the addon is enabled, but TV support still
  // disabled. In that case the addon is not loaded, so we should not try
  // to change its settings.
  if (!XBMC || !g_client)
    return ADDON_STATUS_OK;

  ADDON_STATUS status = settings.SetValue(settingName, settingValue);
  if (status == ADDON_STATUS_NEED_SETTINGS)
  {
    status = ADDON_STATUS_OK;
    // need to trigger recording update;
    g_client->ForceRecordingUpdate();
  }

  return status;
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

void OnSystemSleep()
{
  if (g_client)
    g_client->OnSystemSleep();
}

void OnSystemWake()
{
  if (g_client)
    g_client->OnSystemWake();
}

void OnPowerSavingActivated()
{
}

void OnPowerSavingDeactivated()
{
}

//-- GetCapabilities -----------------------------------------------------
// Tell XBMC our requirements
//-----------------------------------------------------------------------------
PVR_ERROR GetCapabilities(PVR_ADDON_CAPABILITIES *pCapabilities)
{
  XBMC->Log(LOG_DEBUG, "->GetProperties()");

  pCapabilities->bSupportsEPG                = true;
  pCapabilities->bSupportsRecordings         = true;
  pCapabilities->bSupportsRecordingsUndelete = false;
  pCapabilities->bSupportsRecordingSize = settings.m_showRecordingSize;
  pCapabilities->bSupportsTimers             = true;
  pCapabilities->bSupportsTV                 = true;
  pCapabilities->bSupportsRadio              = true;
  pCapabilities->bSupportsChannelGroups      = true;
  pCapabilities->bHandlesInputStream         = true;
  pCapabilities->bHandlesDemuxing            = false;
  pCapabilities->bSupportsChannelScan        = false;
  pCapabilities->bSupportsLastPlayedPosition = true;
  pCapabilities->bSupportsRecordingEdl       = true;
  pCapabilities->bSupportsRecordingsRename   = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo = false;
  pCapabilities->bSupportsRecordingPlayCount = true;

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES *pProperties)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

//-- GetBackendName -----------------------------------------------------------
// Return the Name of the Backend
//-----------------------------------------------------------------------------
const char * GetBackendName(void)
{
  if (g_client)
    return g_client->GetBackendName();
  else
    return "";
}

//-- GetBackendVersion --------------------------------------------------------
// Return the Version of the Backend as String
//-----------------------------------------------------------------------------
const char * GetBackendVersion(void)
{
  if (g_client)
    return g_client->GetBackendVersion();
  else
    return "";
}

//-- GetConnectionString ------------------------------------------------------
// Return a String with connection info, if available
//-----------------------------------------------------------------------------
const char * GetConnectionString(void)
{
  if (g_client)
    return g_client->GetConnectionString();
  else
    return "addon error!";
}

//-- GetBackendHostname -------------------------------------------------------
// Return a String with the backend host name
//-----------------------------------------------------------------------------
const char * GetBackendHostname(void)
{
  return settings.m_hostname.c_str();
}

//-- GetDriveSpace ------------------------------------------------------------
// Return the Total and Free Drive space on the PVR Backend
//-----------------------------------------------------------------------------
PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetDriveSpace(iTotal, iUsed);
}

PVR_ERROR OpenDialogChannelScan()
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->CallMenuHook(menuhook, item);
}


/*******************************************/
/** PVR EPG Functions                     **/

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, int iChannelUid, time_t iStart, time_t iEnd)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetEpg(handle, iChannelUid, iStart, iEnd);
}


/*******************************************/
/** PVR Channel Functions                 **/

int GetChannelsAmount()
{
  if (!g_client)
    return 0;
  else
    return g_client->GetNumChannels();
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetChannels(handle, bRadio);
}

PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR RenameChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channelinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channelinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/*******************************************/
/** PVR Channel group Functions           **/

int GetChannelGroupsAmount(void)
{
  if (!g_client)
    return 0;
  else
    return g_client->GetChannelGroupsAmount();
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetChannelGroups(handle, bRadio);
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetChannelGroupMembers(handle, group);
}


/*******************************************/
/** PVR Recording Functions               **/

int GetRecordingsAmount(bool deleted)
{
  if (!g_client)
    return 0;
  else
    return g_client->GetNumRecordings();
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetRecordings(handle);
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->DeleteRecording(recording);
}

PVR_ERROR RenameRecording(const PVR_RECORDING &recording)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/*******************************************/
/** PVR Timer Functions                   **/

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  if (g_client)
    return g_client->GetTimerTypes(types, size);
  return PVR_ERROR_SERVER_ERROR;
}

int GetTimersAmount(void)
{
  if (!g_client)
    return 0;
  else
    return g_client->GetNumTimers();
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetTimers(handle);
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->AddTimer(timer);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->DeleteTimer(timer, bForceDelete);
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->UpdateTimer(timer);
}


/*******************************************/
/** PVR Live Stream Functions             **/

bool OpenLiveStream(const PVR_CHANNEL &channelinfo)
{
  if (!g_client)
    return false;
  else
    return g_client->OpenLiveStream(channelinfo);
}

void CloseLiveStream()
{
  if (g_client)
    g_client->CloseLiveStream();
}

int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  if (!g_client)
    return 0;
  else
    return g_client->ReadLiveStream(pBuffer, iBufferSize);
}

long long SeekLiveStream(long long iPosition, int iWhence)
{
  if (!g_client)
    return -1;
  else
    return g_client->SeekLiveStream(iPosition, iWhence);
}

long long LengthLiveStream(void)
{
  if (!g_client)
    return -1;
  else
    return g_client->LengthLiveStream();
}

PVR_ERROR GetSignalStatus(int channelUid, PVR_SIGNAL_STATUS *signalStatus)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetSignalStatus(signalStatus);
}

PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL* channel, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  if (g_client)
    return g_client->GetChannelStreamProperties(*channel,properties,iPropertiesCount);
  return PVR_ERROR_NOT_IMPLEMENTED;
}

/*******************************************/
/** PVR Recording Stream Functions        **/

bool OpenRecordedStream(const PVR_RECORDING &recording)
{
  if (!g_client)
    return false;
  else
    return g_client->OpenRecordedStream(recording);
}

void CloseRecordedStream(void)
{
  if (g_client)
    g_client->CloseRecordedStream();
}

int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  if (!g_client)
    return 0;
  else
    return g_client->ReadRecordedStream(pBuffer, iBufferSize);
}

long long SeekRecordedStream(long long iPosition, int iWhence)
{
  if (!g_client)
    return -1;
  else
    return g_client->SeekRecordedStream(iPosition, iWhence);
}

long long LengthRecordedStream(void)
{
  if (!g_client)
    return -1;
  else
    return g_client->LengthRecordedStream();
}

bool CanPauseStream(void)
{
  if (g_client)
    return g_client->CanPauseStream();
  return false;
}

void PauseStream(bool bPaused)
{
  if (g_client)
    g_client->PauseStream(bPaused);
}

bool CanSeekStream(void)
{
  if (g_client)
    return g_client->CanPauseStream();
  return false;
}

PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition)
{
  if (g_client)
    return g_client->SetRecordingLastPlayedPosition(recording, lastplayedposition);
  return PVR_ERROR_SERVER_ERROR;
}

int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
  if (g_client)
    return g_client->GetRecordingLastPlayedPosition(recording);
  return -1;
}

PVR_ERROR GetRecordingEdl(const PVR_RECORDING &recording, PVR_EDL_ENTRY entries[], int *size)
{
  if (g_client)
    return g_client->GetRecordingEdl(recording, entries, size);
  return PVR_ERROR_SERVER_ERROR;
}

bool IsRealTimeStream(void)
{
  if (g_client)
    return g_client->IsRealTimeStream();
  return false;
}

PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  if (g_client)
    return g_client->GetStreamTimes(stimes);
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetStreamReadChunkSize(int* chunksize)
{
  if (g_client)
    return g_client->GetStreamReadChunkSize(chunksize);
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING& recording, int count)
{
  XBMC->Log(LOG_DEBUG, "Play count %s %d", recording.strTitle, count);
  return PVR_ERROR_NO_ERROR;
}

/** UNUSED API FUNCTIONS */
DemuxPacket* DemuxRead(void) { return NULL; }
void DemuxAbort(void) {}
void DemuxReset(void) {}
void DemuxFlush(void) {}
void FillBuffer(bool mode) {}
bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {};
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingSize(const PVR_RECORDING* recording, int64_t* sizeInBytes) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetDescrambleInfo(int, PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagEdl(const EPG_TAG* epgTag, PVR_EDL_ENTRY edl[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }

} //end extern "C"

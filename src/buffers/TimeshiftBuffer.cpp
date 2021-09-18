/*
 *  Copyright (C) 2015-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2015 Sam Stenvall
 *  Copyright (C) 2017 Mike Burgett [modifications to use memory-ring buffer for server-side tsb]
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

/*
 * Block request and processing logic copied from liveshift.cpp and
 * RingBuffer.cpp which are Copyright (C) Team XBMC and distributed
 * under the same license.
 */

#include "TimeshiftBuffer.h"
#include <kodi/General.h>

using namespace timeshift;


const int TimeshiftBuffer::INPUT_READ_LENGTH = 32768;
const int TimeshiftBuffer::BUFFER_BLOCKS = 48;
const int TimeshiftBuffer::WINDOW_SIZE = std::max(6, (BUFFER_BLOCKS/2));

TimeshiftBuffer::TimeshiftBuffer()
  : Buffer(), m_circularBuffer(INPUT_READ_LENGTH * BUFFER_BLOCKS),
    m_seek(&m_sd, &m_circularBuffer), m_streamingclient(nullptr), m_CanPause(true)
{
  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer created!");
  m_sd.lastKnownLength.store(0);
  m_sd.ptsBegin.store(0);
  m_sd.ptsEnd.store(0);
  m_sd.tsbStart.store(0);
  m_sd.streamPosition.store(0);
  m_sd.iBytesPerSecond = 0;
  m_sd.sessionStartTime.store(0);
  m_sd.tsbStartTime.store(0);
  m_sd.tsbRollOff = 0;
  m_sd.lastBlockBuffered = 0;
  m_sd.lastBufferTime = 0;
  m_sd.currentWindowSize = 0;
  m_sd.requestNumber = 0;
  m_sd.requestBlock = 0;
  m_sd.isPaused = false;
  m_sd.pauseStart = 0;
  m_sd.lastPauseAdjust = 0;
}

TimeshiftBuffer::~TimeshiftBuffer()
{
  TimeshiftBuffer::Close();
}

bool TimeshiftBuffer::Open(const std::string inputUrl)
{
  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::Open()");
  Buffer::Open(""); // To set the time stream starts
  m_sd.sessionStartTime.store(m_startTime);
  m_sd.tsbStartTime.store(m_sd.sessionStartTime.load());
  m_streamingclient = new NextPVR::Socket(NextPVR::af_inet, NextPVR::pf_inet, NextPVR::sock_stream, NextPVR::tcp);
  if (!m_streamingclient->create())
  {
    kodi::Log(ADDON_LOG_ERROR, "%s:%d: Could not connect create streaming socket", __FUNCTION__, __LINE__);
    return false;
  }

  if (!m_streamingclient->connect(m_settings.m_hostname, m_settings.m_port))
  {
    kodi::Log(ADDON_LOG_ERROR, "%s:%d: Could not connect to NextPVR backend (%s:%d) for streaming", __FUNCTION__, __LINE__, m_settings.m_hostname.c_str(), m_settings.m_port);
    return false;
  }

  m_streamingclient->send(inputUrl.c_str(), strlen(inputUrl.c_str()));

  char line[256];

  sprintf(line, "Connection: close\r\n");
  m_streamingclient->send(line, strlen(line));

  sprintf(line, "\r\n");
  m_streamingclient->send(line, strlen(line));

  //m_currentLivePosition = 0;


  char buf[1024];
  int read = m_streamingclient->receive(buf, sizeof buf, 0);

  if (read < 0)
    return false;

  for (int i=0; i<read; i++)
  {
    if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n')
    {
      int remainder = read - (i+4);
      if (remainder > 0)
      {
        kodi::Log(ADDON_LOG_DEBUG, "remainder: %s", &buf[i+4]);
        WriteData((byte *)&buf[i+4], remainder, 0);
      }

      char header[256];
      if (i < sizeof(header))
      {
        memset(header, 0, sizeof(header));
        memcpy(header, buf, i);
        kodi::Log(ADDON_LOG_DEBUG, "%s", header);

        if (strstr(header, "HTTP/1.1 404") != NULL)
        {
          kodi::Log(ADDON_LOG_DEBUG, "Unable to start channel. 404");
          kodi::QueueNotification(QUEUE_ERROR, kodi::GetLocalizedString(30053), "");
          return false;
        }

      }

      m_streamingclient->set_non_blocking(0);

      break;
    }
  }

  kodi::Log(ADDON_LOG_DEBUG, "TSB: Opened streaming connection!");
  // Start the input thread
  m_inputThread = std::thread([this]()
  {
    ConsumeInput();
  });

  m_tsbThread = std::thread([this]()
  {
    TSBTimerProc();
  });

  kodi::Log(ADDON_LOG_DEBUG, "Open grabbing lock");
  std::unique_lock<std::mutex> lock(m_mutex);
  kodi::Log(ADDON_LOG_DEBUG, "Open Continuing");
  int minLength = BUFFER_BLOCKS * INPUT_READ_LENGTH;
  kodi::Log(ADDON_LOG_DEBUG, "Open waiting for %d bytes to buffer", minLength);
  m_reader.wait_for(lock, std::chrono::seconds(1),
                    [this, minLength]()
  {
    return m_circularBuffer.BytesAvailable() >= minLength;
  });
  kodi::Log(ADDON_LOG_DEBUG, "Open Continuing %d / %d", m_circularBuffer.BytesAvailable(), minLength);
  // Make sure data is flowing, before declaring success.
  if (m_circularBuffer.BytesAvailable() != 0)
    return true;
  else
    return false;
}

void TimeshiftBuffer::Close()
{
  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::Close()");
  // Wait for the input thread to terminate
  Buffer::Close();

  m_writer.notify_one();  // In case it's sleeping.

  if (m_inputThread.joinable())
    m_inputThread.join();

  if (m_tsbThread.joinable())
    m_tsbThread.join();


  if (m_streamingclient)
  {
    m_streamingclient->close();
    m_streamingclient = nullptr;
  }

  // Reset session data
  m_sd.requestBlock = 0;
  m_sd.requestNumber = 0;
  m_sd.lastKnownLength.store(0);
  m_sd.ptsBegin.store(0);
  m_sd.ptsEnd.store(0);
  m_sd.tsbStart.store(0);
  m_sd.sessionStartTime.store(0);
  m_sd.tsbStartTime.store(0);
  m_sd.tsbRollOff = 0;
  m_sd.iBytesPerSecond = 0;
  m_sd.lastBlockBuffered = 0;
  m_sd.lastBufferTime = 0;
  m_sd.streamPosition.store(0);
  m_sd.currentWindowSize = 0;
  m_sd.inputBlockSize = INPUT_READ_LENGTH;
  m_sd.isPaused = false;
  m_sd.pauseStart = 0;
  m_sd.lastPauseAdjust = 0;
  m_circularBuffer.Reset();

  Reset();
}

void TimeshiftBuffer::Reset()
{
  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::Reset()");

  // Close any open handles
  std::unique_lock<std::mutex> lock(m_mutex);
  m_circularBuffer.Reset();

  // Reset
  m_seek.Clear();

}

ssize_t TimeshiftBuffer::Read(byte *buffer, size_t length)
{
  int bytesRead = 0;
  std::unique_lock<std::mutex> lock(m_mutex);
  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::Read() %d @ %lli", length, m_sd.streamPosition.load());

  // Wait until we have enough data

  if (! m_reader.wait_for(lock, std::chrono::seconds(m_readTimeout),
    [this, length]()
  {
    return m_circularBuffer.BytesAvailable() >= (int )length;
  }))
  {
    kodi::Log(ADDON_LOG_DEBUG, "Timeout waiting for bytes!! [buffer underflow]");
  }
  bytesRead = m_circularBuffer.ReadBytes(buffer, length);
  m_sd.streamPosition.fetch_add(length);
  if (m_circularBuffer.BytesFree() >= INPUT_READ_LENGTH)
  {
    // wake the filler thread if there's room in the buffer
    m_writer.notify_one();
  }

  if (bytesRead != length)
    kodi::Log(ADDON_LOG_DEBUG, "Read returns %d for %d request.", bytesRead, length);
  return bytesRead;
}

//
// When seeking, we're going to have to flush the buffers already in transit (already requested)
// and only make data available when we get the seeked-to block in.
//

int64_t TimeshiftBuffer::Seek(int64_t position, int whence)
{
  bool sleep = false;
  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::Seek()");
  int64_t highLimit = m_sd.lastKnownLength.load() - m_sd.iBytesPerSecond;
  int64_t lowLimit = m_sd.tsbStart.load() + (m_sd.iBytesPerSecond << 2);  // Add Roughly 4 seconds to account for estimating the start.

  if (position > highLimit)
  {
    kodi::Log(ADDON_LOG_ERROR, "Seek requested to %lld, limiting to %lld\n", position, highLimit);
    position = highLimit;
  }
  else if (position < lowLimit)
  {
    kodi::Log(ADDON_LOG_ERROR, "Seek requested to %lld, limiting to %lld\n", position, lowLimit);
    position = lowLimit;
  }

  {
    std::unique_lock<std::mutex> lock(m_mutex);
    // m_streamPositon is the offset in the stream that will be read next,
    // so if that matches the seek position, don't seek.
    kodi::Log(ADDON_LOG_DEBUG, "Seek:  %d  %d  %llu %llu", SEEK_SET, whence, m_sd.streamPosition.load(), position);
    if ((whence == SEEK_SET) && (position == m_sd.streamPosition.load()))
      return position;
    m_seek.InitSeek(position, whence);
    if (m_seek.PreprocessSeek())
    {
      internalRequestBlocks();
      m_writer.notify_one(); // wake consumer.
      sleep = true;
    }
  }
  if (sleep)
  {
    std::unique_lock<std::mutex> sLock(m_sLock);
    kodi::Log(ADDON_LOG_DEBUG, "Seek Waiting");
    m_seeker.wait(sLock);
  }
  kodi::Log(ADDON_LOG_DEBUG, "Seek() returning %lli", position);
  return position;
}

PVR_ERROR TimeshiftBuffer::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  stimes.SetStartTime(m_sd.sessionStartTime.load());
  stimes.SetPTSStart(0);
  stimes.SetPTSBegin(m_sd.ptsBegin.load());
  stimes.SetPTSEnd(m_sd.ptsEnd.load());
//  kodi::Log(ADDON_LOG_ERROR, "GetStreamTimes: %d, %lli, %lli, %lli",
//            stimes->startTime, stimes->ptsStart, stimes->ptsBegin, stimes->ptsEnd);
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR TimeshiftBuffer::GetStreamReadChunkSize(int& chunksize)
{
  // Make this a tunable parameter?
  chunksize = TimeshiftBuffer::INPUT_READ_LENGTH;
  return PVR_ERROR_NO_ERROR;
}

void TimeshiftBuffer::RequestBlocks()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  internalRequestBlocks();
}

void TimeshiftBuffer::internalRequestBlocks()
{
  //  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::RequestBlocks()");

  m_seek.ProcessRequests(); // Handle outstanding seek request, if there is one.

  // send read request (using a basic sliding window protocol)
  for (int i = m_sd.currentWindowSize; i < WINDOW_SIZE; i++)
  {
    int64_t blockOffset = m_sd.requestBlock;
    char request[48];
    memset(request, 0, sizeof(request));
    snprintf(request, sizeof(request), "Range: bytes=%llu-%llu-%d", blockOffset, (blockOffset+INPUT_READ_LENGTH), m_sd.requestNumber);
    kodi::Log(ADDON_LOG_DEBUG, "sending request: %s\n", request);
    if (m_streamingclient->send(request, sizeof(request)) != sizeof(request))
    {
      kodi::Log(ADDON_LOG_DEBUG, "NOT ALL BYTES SENT!");
    }

    m_sd.requestBlock += INPUT_READ_LENGTH;
    m_sd.requestNumber++;
    m_sd.currentWindowSize++;
  }
}

uint32_t TimeshiftBuffer::WatchForBlock(byte *buffer, uint64_t *block)
{
//  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::WatchForBlock()");

  int64_t watchFor = -1;  // Any (next) block
  uint32_t returnBytes = 0;
  int retries = WINDOW_SIZE + 1;
  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_seek.Active())
  {
    if (m_seek.BlockRequested())
    { // Can't watch for blocks that haven't been requested!
      watchFor = m_seek.SeekStreamOffset();
      kodi::Log(ADDON_LOG_DEBUG, "%s:%d: watching for bloc %llu", __FUNCTION__, __LINE__, watchFor);
    }
    else
    {
      return returnBytes;
    }
  }
  //if (watchFor == -1)
  //  kodi::Log(ADDON_LOG_DEBUG, "waiting for next block");
  //else
  //  kodi::Log(ADDON_LOG_DEBUG, "about to wait for block with offset: %llu\n", watchFor);
  while (retries)
  {
    if (!m_streamingclient->is_valid())
    {
      kodi::Log(ADDON_LOG_DEBUG, "about to call receive(), socket is invalid\n");
      return returnBytes;
    }

    if (m_streamingclient->read_ready())
    {
      // read response header
      char response[128];
      memset(response, 0, sizeof(response));
      int responseByteCount = m_streamingclient->receive(response, sizeof(response), sizeof(response));
      kodi::Log(ADDON_LOG_DEBUG, "%s:%d: responseByteCount: %d\n", __FUNCTION__, __LINE__, responseByteCount);
      if (responseByteCount > 0)
      {
        kodi::Log(ADDON_LOG_DEBUG, "%s:%d: got: %s\n", __FUNCTION__, __LINE__, response);
      }
      else if (responseByteCount < 0)
      {
        return 0;
      }
  #if defined(TARGET_WINDOWS)
      else if (responseByteCount < 0 && errno == WSAEWOULDBLOCK)
  #else
      else if (responseByteCount < 0 && errno == EAGAIN)
  #endif
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        kodi::Log(ADDON_LOG_DEBUG, "got: %d", errno);
        retries--;
        continue;
      }

      // parse response header
      long long payloadOffset;
      int payloadSize;
      long long fileSize;
      int dummy;
      sscanf(response, "%llu:%d %llu %d", &payloadOffset, &payloadSize, &fileSize, &dummy);
      kodi::Log(ADDON_LOG_DEBUG, "PKT_IN: %llu:%d %llu %d", payloadOffset, payloadSize, fileSize, dummy);
      if (m_sd.lastKnownLength.load() != fileSize)
      {
        m_sd.lastKnownLength.store(fileSize);
      }

      // read response payload
      int bytesRead = 0;

      do
      {
        bytesRead = m_streamingclient->receive((char *)buffer, INPUT_READ_LENGTH, payloadSize);
#if defined(TARGET_WINDOWS)
      } while (bytesRead < 0 && errno == WSAEWOULDBLOCK);
#else
      } while (bytesRead < 0 && errno == EAGAIN);
#endif

      if ((watchFor == -1) || (payloadOffset == watchFor))
      {
        if (m_circularBuffer.BytesAvailable() == 0) // Buffer empty!
          m_sd.streamPosition.store(payloadOffset);
        *block = payloadOffset;
        returnBytes = payloadSize;
        if (m_sd.currentWindowSize > 0)
          m_sd.currentWindowSize--;
        kodi::Log(ADDON_LOG_DEBUG, "Returning block %llu for buffering", payloadOffset);
        break; // We want to buffer this payload.
      }
    }
  }
  return returnBytes;
}
/* Write data to ring buffer from buffer specified in 'buf'. Amount read in is
 * specified by 'size'.
 */
bool TimeshiftBuffer::WriteData(const byte *buf, unsigned int size, uint64_t blockNum)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_circularBuffer.WriteBytes(buf, size))
  {
    m_sd.lastBlockBuffered = blockNum;
    return true;
  }
  kodi::Log(ADDON_LOG_ERROR, "%s:%d: Error writing block to circularBuffer!", __FUNCTION__, __LINE__);
  return false;
 }

 void TimeshiftBuffer::TSBTimerProc()
 {
   // ONLY use atomic types/ops inR session_data, don't mess with
   // the locks!
   while (m_active)
   {
     std::this_thread::sleep_for(std::chrono::seconds(1));  // Let's try 1 per second.

     // First, take a snapshot
     time_t now = time(NULL);
     time_t sessionStartTime = m_sd.sessionStartTime.load();
     time_t tsbStartTime = m_sd.tsbStartTime.load();
     int64_t lastKnownLength = m_sd.lastKnownLength.load();
     uint64_t streamPosition = m_sd.streamPosition.load();
     int64_t tsbStart = m_sd.tsbStart.load();
     time_t iBytesPerSecond = m_sd.iBytesPerSecond;
     bool isPaused = m_sd.isPaused;
     time_t pauseStart = m_sd.pauseStart;
     time_t lastPauseAdjust = m_sd.lastPauseAdjust;

     if (tsbStartTime == 0)
     {
       tsbStartTime = sessionStartTime;
     }

     // Now perform the calculations
     time_t elapsed = now - tsbStartTime;
     //kodi::Log(ADDON_LOG_ERROR, "TSBTimerProc: time_diff: %d, tsbStartTime: %d", elapsed, tsbStartTime);
     if (elapsed > m_settings.m_timeshiftBufferSeconds)
     {
       // Roll the tsb forward
       int tsbRoll = elapsed - m_settings.m_timeshiftBufferSeconds;
       elapsed = m_settings.m_timeshiftBufferSeconds;
       tsbStart += (tsbRoll * iBytesPerSecond);
       tsbStartTime += tsbRoll;
       // kodi::Log(ADDON_LOG_ERROR, "startTime: %d, start: %lli, isPaused: %d, tsbRoll: %d", tsbStartTime, tsbStart, isPaused, tsbRoll);
     }
     if (m_sd.isPaused)
     {
       if ((now > pauseStart) && (now > lastPauseAdjust))
       { // If we're paused, we stop requesting/receiving buffers, so lastKnownLength doesn't get updated. Fudge it here.
         lastKnownLength += ((now - lastPauseAdjust) * iBytesPerSecond);
         lastPauseAdjust = now;
       }
     }

     int totalTime = now - sessionStartTime;                // total seconds we've been tuned to this channel.
     iBytesPerSecond = totalTime ? (int )(lastKnownLength / totalTime) : 0;  // lastKnownLength (total bytes buffered) / number_of_seconds buffered.

     // Write everything back
     m_sd.tsbStartTime.store(tsbStartTime);
     m_sd.tsbStart.store(tsbStart);
     m_sd.lastKnownLength.store(lastKnownLength);
     m_sd.iBytesPerSecond = iBytesPerSecond;
     m_sd.ptsBegin.store((tsbStartTime - sessionStartTime) * STREAM_TIME_BASE);
     m_sd.ptsEnd.store((now - sessionStartTime) * STREAM_TIME_BASE);
     m_sd.lastPauseAdjust = lastPauseAdjust;


//     kodi::Log(ADDON_LOG_ERROR, "tsb_start: %lli, end: %llu, B/sec: %d",
//               tsbStart, lastKnownLength, iBytesPerSecond);

   }
 }

void TimeshiftBuffer::ConsumeInput()
{
  kodi::Log(ADDON_LOG_DEBUG, "TimeshiftBuffer::ConsumeInput()");
  byte *buffer = new byte[INPUT_READ_LENGTH];

  while (m_active)
  {
    memset(buffer, 0, INPUT_READ_LENGTH);

    RequestBlocks();

    uint32_t read;
    uint64_t blockNo;
    while ((read = WatchForBlock(buffer, &blockNo)))
    {
//      kodi::Log(ADDON_LOG_DEBUG, "Processing %d byte block", read);
      if (WriteData(buffer, read, blockNo))
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        //kodi::Log(ADDON_LOG_DEBUG, "Data Buffered");
        //kodi::Log(ADDON_LOG_DEBUG, "Notifying reader");
        // Signal that we have data again
        if (m_seek.Active())
        {
          if (m_seek.PostprocessSeek(blockNo))
          {
            kodi::Log(ADDON_LOG_DEBUG, "Notify Seek");
            m_seeker.notify_one();
          }
        }
        m_reader.notify_one();
      }
      else
      {
        kodi::Log(ADDON_LOG_DEBUG, "Error Buffering Data!!");
      }
      std::this_thread::yield();
      std::unique_lock<std::mutex> lock(m_mutex);
      if (m_circularBuffer.BytesFree() < INPUT_READ_LENGTH)
      {
        m_writer.wait(lock, [this]()
        {
          return (!m_active || (m_circularBuffer.BytesFree() >= INPUT_READ_LENGTH));
        });
      }
      if (!m_active || ((blockNo + INPUT_READ_LENGTH) == m_sd.requestBlock))
        break;
    }
  }
  kodi::Log(ADDON_LOG_DEBUG, "CONSUMER THREAD IS EXITING!!!");
  delete[] buffer;
}

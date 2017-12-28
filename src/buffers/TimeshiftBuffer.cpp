/*
*      Copyright (C) 2015 Sam Stenvall
*      Copyright (C) 2017 Mike Burgett [modifications to use memory-ring buffer
*         for server-side tsb]
*
*      Block request and processing logic copied from liveshift.cpp and
*      RingBuffer.cpp which are Copyright (C) Team XBMC and distributed
*      under the same license.
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
*  along with XBMC; see the file COPYING.  If not, write to
*  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
*  MA 02110-1301  USA
*  http://www.gnu.org/copyleft/gpl.html
*
*/

#include "TimeshiftBuffer.h"
#include <cstring>
#include "p8-platform/os.h"

using namespace timeshift;
using namespace ADDON;

const int TimeshiftBuffer::INPUT_READ_LENGTH = 32768;
const int TimeshiftBuffer::BUFFER_BLOCKS = 12;
const int TimeshiftBuffer::WINDOW_SIZE = std::max(6, (BUFFER_BLOCKS/2));

// Fix a stupid #define on Windows which causes XBMC->DeleteFile() to break
#ifdef _WIN32
#undef DeleteFile
#endif // _WIN32

TimeshiftBuffer::TimeshiftBuffer()
  : Buffer(), m_circularBuffer(INPUT_READ_LENGTH * BUFFER_BLOCKS), 
    m_seek(&m_sd, &m_circularBuffer), m_streamingclient(nullptr), m_tsbStartTime(0)
{
  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer created!");
  m_sd.lastKnownLength.store(0);
  m_sd.tsbStart.store(0);
  m_sd.streamPosition.store(0);
  m_sd.iBytesPerSecond = 0;
  m_sd.sessionStartTime = 0;
  m_sd.tsbStartTime = 0;
  m_sd.tsbRollOff = 0;
  m_sd.lastBlockBuffered = 0;
  m_sd.lastBufferTime = 0;
  m_sd.currentWindowSize = 0;
  m_sd.requestNumber = 0;
  m_sd.requestBlock = 0;
}

TimeshiftBuffer::~TimeshiftBuffer()
{
  TimeshiftBuffer::Close();
}

bool TimeshiftBuffer::Open(const std::string inputUrl)
{
  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::Open()");
  Buffer::Open(""); // To set the time stream starts
  m_sd.tsbStartTime = m_sd.sessionStartTime = Buffer::GetStartTime();
  m_streamingclient = new NextPVR::Socket(NextPVR::af_inet, NextPVR::pf_inet, NextPVR::sock_stream, NextPVR::tcp);
  if (!m_streamingclient->create())
  {
    XBMC->Log(LOG_ERROR, "%s:%d: Could not connect create streaming socket", __FUNCTION__, __LINE__);
    return false;
  }

  if (!m_streamingclient->connect(g_szHostname, g_iPort))
  {
    XBMC->Log(LOG_ERROR, "%s:%d: Could not connect to NextPVR backend (%s:%d) for streaming", __FUNCTION__, __LINE__, g_szHostname.c_str(), g_iPort);
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


  for (int i=0; i<read; i++)
  {
    if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n')
    {
      int remainder = read - (i+4);
      if (remainder > 0)
      {
        XBMC->Log(LOG_DEBUG, "remainder: %s", &buf[i+4]);
        WriteData((byte *)&buf[i+4], remainder, 0);
      }

      char header[256];
      if (i < sizeof(header))
      {
        memset(header, 0, sizeof(header));
        memcpy(header, buf, i);
        XBMC->Log(LOG_DEBUG, "%s", header);

        if (strstr(header, "HTTP/1.1 404") != NULL)
        {
          XBMC->Log(LOG_DEBUG, "Unable to start channel. 404");
          XBMC->QueueNotification(QUEUE_INFO, "Tuner not available");
          return false;
        }

      }

      m_streamingclient->set_non_blocking(0); 
 
      break;
    }
  }

  XBMC->Log(LOG_DEBUG, "TSB: Opened streaming connection!");
  // Start the input thread
  m_inputThread = std::thread([this]()
  {
    ConsumeInput();
  });
  
  XBMC->Log(LOG_DEBUG, "Open grabbing lock");
  std::unique_lock<std::mutex> lock(m_mutex);
  XBMC->Log(LOG_DEBUG, "Open Continuing");
  int minLength = BUFFER_BLOCKS * INPUT_READ_LENGTH;
  XBMC->Log(LOG_DEBUG, "Open waiting for %d bytes to buffer", minLength);
  m_reader.wait_for(lock, std::chrono::seconds(2),
                    [this, minLength]()
  {
    return m_circularBuffer.BytesAvailable() >= minLength;
  });
  XBMC->Log(LOG_DEBUG, "Open Continuing");
  // Make sure data is flowing, before declaring success.
  if (m_circularBuffer.BytesAvailable() != 0)
    return true;
  else
    return false;
}

void TimeshiftBuffer::Close()
{
  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::Close()");
  // Wait for the input thread to terminate
  Buffer::Close();
  
  m_writer.notify_one();  // In case it's sleeping.

  if (m_inputThread.joinable())
    m_inputThread.join();
  
  if (m_streamingclient)
  {
    m_streamingclient->close();
    m_streamingclient = nullptr;
  }

  // Reset session data
  m_sd.requestBlock = 0;
  m_sd.requestNumber = 0;
  m_sd.lastKnownLength.store(0);
  m_sd.tsbStart.store(0);
  m_sd.sessionStartTime = 0;
  m_sd.tsbStartTime = 0;
  m_sd.tsbRollOff = 0;
  m_sd.iBytesPerSecond = 0;
  m_sd.lastBlockBuffered = 0;
  m_sd.lastBufferTime = 0;
  m_sd.streamPosition.store(0);
  m_sd.currentWindowSize = 0;
  m_sd.inputBlockSize = INPUT_READ_LENGTH;
  m_circularBuffer.Reset();

  Reset();
}

void TimeshiftBuffer::Reset()
{
  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::Reset()");

  // Close any open handles
  std::unique_lock<std::mutex> lock(m_mutex);
  m_circularBuffer.Reset();

  // Reset
  m_seek.Clear();

}

int TimeshiftBuffer::Read(byte *buffer, size_t length)
{
  int bytesRead = 0;
  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::Read() %d @ %lli", length, m_sd.streamPosition.load());

  // Wait until we have enough data
  
  std::unique_lock<std::mutex> lock(m_mutex);
  m_reader.wait_for(lock, std::chrono::seconds(m_readTimeout),
    [this, length]()
  {
    return m_circularBuffer.BytesAvailable() >= length;
  });
  
  bytesRead = m_circularBuffer.ReadBytes(buffer, length);
  m_sd.streamPosition.fetch_add(length);
  if (m_circularBuffer.BytesFree() >= INPUT_READ_LENGTH)
  {
    // wake the filler thread if there's room in the buffer
    m_writer.notify_one();
  }

  return bytesRead;
}

//
// When seeking, we're going to have to flush the buffers already in transit (already requested)
// and only make data available when we get the seeked-to block in.
//

int64_t TimeshiftBuffer::Seek(int64_t position, int whence)
{
  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::Seek()");

  std::unique_lock<std::mutex> lock(m_mutex);
  // m_streamPositon is the offset in the stream that will be read next,
  // so if that matches the seek position, don't seek.
  XBMC->Log(LOG_DEBUG, "Seek:  %d  %d  %llu %llu", SEEK_SET, whence, m_sd.streamPosition.load(), position);
  if ((whence == SEEK_SET) && (position == m_sd.streamPosition.load()))
    return position;
  m_seek.InitSeek(position, whence);
  if (m_seek.PreprocessSeek())
  {
    internalRequestBlocks();
    m_writer.notify_one(); // wake consumer.
    m_seeker.wait(lock);
  }
  XBMC->Log(LOG_DEBUG, "Seek() returning %lli", position);
  return position;
}

time_t TimeshiftBuffer::GetStartTime()
{
  if (m_active)
  {
    if (m_sd.tsbStartTime == 0)
    {
      m_sd.tsbStartTime = Buffer::GetStartTime();
    }
    time_t now = time(nullptr);
    int time_diff = now - m_sd.tsbStartTime;
    XBMC->Log(LOG_DEBUG, "time_diff: %d, m_tsbStartTime: %d", time_diff, m_sd.tsbStartTime);
    if (time_diff > g_timeShiftBufferSeconds)
    {
      m_sd.tsbStartTime += (time_diff - g_timeShiftBufferSeconds);
    }
    return m_sd.tsbStartTime;
  }
  return 0;
}

time_t TimeshiftBuffer::GetEndTime()
{
  if (m_active)
    return Buffer::GetEndTime();
  return 0;
}

time_t TimeshiftBuffer::GetPlayingTime()
{
  if (m_active)
  {
    time_t start = GetStartTime();
    time_t now = time(NULL);
    time_t tsbRoll = start - m_sd.sessionStartTime;
    time_t tdiff = tsbRoll - m_sd.tsbRollOff;  // Correction on this visit.
    time_t lbtc = now - m_sd.lastBufferTime;
    XBMC->Log(LOG_DEBUG, "start: %d, lbtc: %d, tdiff: %d", start, lbtc, tdiff);
    if (lbtc > 0)
    { // If we're paused, we stop requesting/receiving buffers, so Length() doesn't get updated. Fudge it here.
      m_sd.lastKnownLength.fetch_add(lbtc * m_sd.iBytesPerSecond);
      m_sd.lastBufferTime = now;
    }
    if (tdiff > 0)
    {
      m_sd.tsbStart.fetch_add(tdiff * m_sd.iBytesPerSecond);
      m_sd.tsbRollOff = tsbRoll;
    }
    uint64_t end = m_sd.lastKnownLength.load();
    int64_t local_tsb_start = m_sd.tsbStart.load();
    int64_t tsb_len = end - local_tsb_start;
    uint64_t pos = Position();
    uint64_t temp = (now - start) * (pos - local_tsb_start);
    int  viewPos = temp ? (temp / tsb_len) : 0;
    XBMC->Log(LOG_DEBUG, "tsb_start: %lli, end: %llu, tsb_len: %lli, viewPos: %d B/sec: %d", 
              local_tsb_start, end, tsb_len, viewPos, m_sd.iBytesPerSecond);
    return start + viewPos;
  }
  return 0;
}


void TimeshiftBuffer::RequestBlocks()
{
  std::unique_lock<std::mutex> lock(m_mutex);
  internalRequestBlocks();
}

void TimeshiftBuffer::internalRequestBlocks()
{
  //  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::RequestBlocks()");

  m_seek.ProcessRequests(); // Handle outstanding seek request, if there is one.

  // send read request (using a basic sliding window protocol)
  for (int i = m_sd.currentWindowSize; i < WINDOW_SIZE; i++)
  {
    int64_t blockOffset = m_sd.requestBlock;
    char request[48];
    memset(request, 0, sizeof(request));
    snprintf(request, sizeof(request), "Range: bytes=%llu-%llu-%d", blockOffset, (blockOffset+INPUT_READ_LENGTH), m_sd.requestNumber);
    int sent;
    XBMC->Log(LOG_DEBUG, "sending request: %s\n", request);
    if (m_streamingclient->send(request, sizeof(request)) != sizeof(request))
    {
      XBMC->Log(LOG_DEBUG, "NOT ALL BYTES SENT!");
    }

    m_sd.requestBlock += INPUT_READ_LENGTH;
    m_sd.requestNumber++;
    m_sd.currentWindowSize++;
  }
}

uint32_t TimeshiftBuffer::WatchForBlock(byte *buffer, uint64_t *block)
{
//  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::WatchForBlock()");

  int64_t watchFor = -1;  // Any (next) block
  uint32_t returnBytes = 0;
  int retries = WINDOW_SIZE + 1;
  std::unique_lock<std::mutex> lock(m_mutex);
  if (m_seek.Active())
  {
    if (m_seek.BlockRequested())
    { // Can't watch for blocks that haven't been requested!
      watchFor = m_seek.SeekStreamOffset();
    }
    else
    {
      return returnBytes;
    }
  }
  //if (watchFor == -1)
  //  XBMC->Log(LOG_DEBUG, "waiting for next block");
  //else
  //  XBMC->Log(LOG_DEBUG, "about to wait for block with offset: %llu\n", watchFor);
  while (retries)
  {
    if (!m_streamingclient->is_valid())
    {
      XBMC->Log(LOG_DEBUG, "about to call receive(), socket is invalid\n");
      return returnBytes;
    }

    if (m_streamingclient->read_ready())
    {
      // read response header
      char response[128];
      memset(response, 0, sizeof(response));
      int responseByteCount = m_streamingclient->receive(response, sizeof(response), sizeof(response));
      if (responseByteCount > 0)
      {
        XBMC->Log(LOG_DEBUG, "%s:%d: got: %s\n", __FUNCTION__, __LINE__, response);
      }
  #if defined(TARGET_WINDOWS)
      else if (responseByteCount < 0 && errno == WSAEWOULDBLOCK)
  #else
      else if (responseByteCount < 0 && errno == EAGAIN)
  #endif
      {
        usleep(50000);
        XBMC->Log(LOG_DEBUG, "got: %d", errno);
        retries--;
        continue;
      }

      // parse response header
      long long payloadOffset;
      int payloadSize;
      long long fileSize;
      int dummy;
      sscanf(response, "%llu:%d %llu %d", &payloadOffset, &payloadSize, &fileSize, &dummy);
      if (m_sd.lastKnownLength.load() != fileSize)
      {
        XBMC->Log(LOG_DEBUG, "Adjust lastKnownLength, and reset m_sd.lastBufferTime!");
        m_sd.lastBufferTime = time(NULL);
        time_t elapsed = m_sd.lastBufferTime - m_sd.sessionStartTime;
        m_sd.iBytesPerSecond = elapsed ? fileSize / elapsed : fileSize; // Running estimate of 1 second worth of stream bytes.
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
        XBMC->Log(LOG_DEBUG, "Returning block %llu for buffering", payloadOffset);
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
  return false;
 }

void TimeshiftBuffer::ConsumeInput()
{
  XBMC->Log(LOG_DEBUG, "TimeshiftBuffer::ConsumeInput()");
  byte *buffer = new byte[INPUT_READ_LENGTH];
  
  while (m_active)
  {
    memset(buffer, 0, INPUT_READ_LENGTH);
    
    RequestBlocks();
     
    uint32_t read;
    uint64_t blockNo;
    while ((read = WatchForBlock(buffer, &blockNo)))
    {
//      XBMC->Log(LOG_DEBUG, "Processing %d byte block", read);
      if (WriteData(buffer, read, blockNo))
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        //XBMC->Log(LOG_DEBUG, "Data Buffered");
        //XBMC->Log(LOG_DEBUG, "Notifying reader");
        // Signal that we have data again
        if (m_seek.Active())
        {
          if (m_seek.PostprocessSeek(blockNo))
          {
            m_seeker.notify_one();
          }
        }
        m_reader.notify_one();
      }
      else
      {
        XBMC->Log(LOG_DEBUG, "Error Buffering Data!!");
      }
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
  XBMC->Log(LOG_DEBUG, "CONSUMER THREAD IS EXITING!!!");
  delete[] buffer;
}

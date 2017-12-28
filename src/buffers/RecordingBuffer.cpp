/*
*      Copyright (C) 2015 Sam Stenvall
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

#include "RecordingBuffer.h"

using namespace timeshift;

PVR_ERROR RecordingBuffer::GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  stimes->startTime = 0;
  stimes->ptsStart = 0;
  stimes->ptsBegin = 0;
  stimes->ptsEnd = ((int64_t )m_Duration) * DVD_TIME_BASE;
  XBMC->Log(LOG_ERROR, "RecordingBuffer::GetStreamTimes called!");
  return PVR_ERROR_NO_ERROR;
}

time_t RecordingBuffer::GetBufferStartTime()
{
  return Buffer::GetStartTime();
}

time_t RecordingBuffer::GetBufferEndTime()
{
  return Buffer::GetEndTime();
}

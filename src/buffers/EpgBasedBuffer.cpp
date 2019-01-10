/*
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

#include "EpgBasedBuffer.h"
#include  "../BackendRequest.h"
#include <thread>
#include <mutex>

#define HTTP_OK 200

using namespace timeshift;

/* EPG Based mode functions */

bool EpgBasedBuffer::Open(const std::string inputUrl)
{
	struct PVR_RECORDING recording;
	recording.recordingTime = time(nullptr);
	recording.iDuration = 5 * 60 * 60;
	m_sd.isPaused = false;
	m_sd.lastPauseAdjust = 0;
	XBMC->Log(LOG_DEBUG, "EpgBasedBuffer::Open In EPG Mode");
	return RecordingBuffer::Open(inputUrl,recording);
}

bool EpgBasedBuffer::CanPauseStream()
{
	if (m_sd.isPaused == true)
	{
		time_t now = time(nullptr);
		if ( m_sd.lastPauseAdjust + 10  >= now )
		{
			m_sd.lastPauseAdjust = now + 10;
			std::string response;
			NextPVR::Request *request;
			request = new NextPVR::Request();
			std::this_thread::yield();
			std::unique_lock<std::mutex> lock(m_mutex);
			if (request->DoRequest("/service?method=channel.transcode.lease", response,m_sid) == HTTP_OK)
			{
				XBMC->Log(LOG_DEBUG, "channel.transcode.lease success");
			}
			else
			{
				XBMC->Log(LOG_ERROR, "channel.transcode.lease failed");
			}
			delete request;
		}
	}
	return true;
}

/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDMessage.h"

#include "DVDDemuxers/DVDDemuxUtils.h"
#include "threads/Condition.h"
#include "threads/CriticalSection.h"
#include "threads/SystemClock.h"
#include "utils/MathUtils.h"
#include "utils/log.h"

#include <algorithm>

using namespace std::chrono_literals;

class CDVDMsgGeneralSynchronizePriv
{
public:
  CDVDMsgGeneralSynchronizePriv(std::chrono::milliseconds timeout, unsigned int sources)
    : sources(sources), reached(0), timeout(timeout)
  {}
  unsigned int sources;
  unsigned int reached;
  CCriticalSection section;
  XbmcThreads::ConditionVariable condition;
  XbmcThreads::EndTime<> timeout;
};

/**
 * CDVDMsgGeneralSynchronize --- GENERAL_SYNCRONIZR
 */
CDVDMsgGeneralSynchronize::CDVDMsgGeneralSynchronize(std::chrono::milliseconds timeout,
                                                     unsigned int sources)
  : CDVDMsg(GENERAL_SYNCHRONIZE), m_p(new CDVDMsgGeneralSynchronizePriv(timeout, sources))
{
}

CDVDMsgGeneralSynchronize::~CDVDMsgGeneralSynchronize()
{
  m_p->condition.notifyAll();

  delete m_p;
}

bool CDVDMsgGeneralSynchronize::Wait(std::chrono::milliseconds milliseconds, unsigned int source)
{
  CSingleLock lock(m_p->section);

  XbmcThreads::EndTime<> timeout{milliseconds};

  m_p->reached |= (source & m_p->sources);
  if ((m_p->sources & SYNCSOURCE_ANY) && source)
    m_p->reached |= SYNCSOURCE_ANY;

  m_p->condition.notifyAll();

  while (m_p->reached != m_p->sources)
  {
    milliseconds = std::min(m_p->timeout.GetTimeLeft(), timeout.GetTimeLeft());
    if (m_p->condition.wait(lock, milliseconds))
      continue;

    if (m_p->timeout.IsTimePast())
    {
      CLog::Log(LOGDEBUG, "CDVDMsgGeneralSynchronize - global timeout");
      return true;  // global timeout, we are done
    }
    if (timeout.IsTimePast())
    {
      return false; /* request timeout, should be retried */
    }
  }
  return true;
}

void CDVDMsgGeneralSynchronize::Wait(std::atomic<bool>& abort, unsigned int source)
{
  while (!Wait(100ms, source) && !abort)
    ;
}

/**
 * CDVDMsgDemuxerPacket --- DEMUXER_PACKET
 */
CDVDMsgDemuxerPacket::CDVDMsgDemuxerPacket(DemuxPacket* packet, bool drop) : CDVDMsg(DEMUXER_PACKET)
{
  m_packet = packet;
  m_drop   = drop;
}

CDVDMsgDemuxerPacket::~CDVDMsgDemuxerPacket()
{
  if (m_packet)
    CDVDDemuxUtils::FreeDemuxPacket(m_packet);
}

unsigned int CDVDMsgDemuxerPacket::GetPacketSize()
{
  if (m_packet)
    return m_packet->iSize;
  else
    return 0;
}

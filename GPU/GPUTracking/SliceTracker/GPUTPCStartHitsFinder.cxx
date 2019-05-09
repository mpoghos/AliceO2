// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file GPUTPCStartHitsFinder.cxx
/// \author Sergey Gorbunov, Ivan Kisel, David Rohr

#include "GPUTPCStartHitsFinder.h"
#include "GPUTPCTracker.h"
#include "GPUCommonMath.h"

using namespace GPUCA_NAMESPACE::gpu;

template <>
GPUd() void GPUTPCStartHitsFinder::Thread<0>(int /*nBlocks*/, int nThreads, int iBlock, int iThread, GPUsharedref() MEM_LOCAL(GPUTPCSharedMemory) & s, processorType& tracker)
{
  // find start hits for tracklets

  if (iThread == 0) {
    s.mIRow = iBlock + 1;
    s.mNRowStartHits = 0;
    if (s.mIRow <= GPUCA_ROW_COUNT - 4) {
      s.mNHits = tracker.Row(s.mIRow).NHits();
    } else {
      s.mNHits = -1;
    }
  }
  GPUbarrier();
  GPUglobalref() const MEM_GLOBAL(GPUTPCRow)& row = tracker.Row(s.mIRow);
  GPUglobalref() const MEM_GLOBAL(GPUTPCRow)& rowUp = tracker.Row(s.mIRow + 2);
  for (int ih = iThread; ih < s.mNHits; ih += nThreads) {
    if (tracker.HitLinkDownData(row, ih) == CALINK_INVAL && tracker.HitLinkUpData(row, ih) != CALINK_INVAL && tracker.HitLinkUpData(rowUp, tracker.HitLinkUpData(row, ih)) != CALINK_INVAL) {
#ifdef GPUCA_SORT_STARTHITS
      GPUglobalref() GPUTPCHitId* const startHits = tracker.TrackletTmpStartHits() + s.mIRow * GPUCA_MAX_ROWSTARTHITS;
      int nextRowStartHits = CAMath::AtomicAddShared(&s.mNRowStartHits, 1);
      if (nextRowStartHits >= GPUCA_MAX_ROWSTARTHITS)
#else
      GPUglobalref() GPUTPCHitId* const startHits = tracker.TrackletStartHits();
      int nextRowStartHits = CAMath::AtomicAdd(tracker.NTracklets(), 1);
      if (nextRowStartHits >= GPUCA_MAX_TRACKLETS)
#endif
      {
        tracker.GPUParameters()->gpuError = GPUCA_ERROR_TRACKLET_OVERFLOW;
        CAMath::AtomicExch(tracker.NTracklets(), 0);
        break;
      }
      startHits[nextRowStartHits].Set(s.mIRow, ih);
    }
  }
  GPUbarrier();

#ifdef GPUCA_SORT_STARTHITS
  if (iThread == 0) {
    int nOffset = CAMath::AtomicAdd(tracker.NTracklets(), s.mNRowStartHits);
    (void)nOffset; // Suppress compiler warning
#ifdef GPUCA_GPUCODE
    tracker.RowStartHitCountOffset()[s.mIRow] = s.mNRowStartHits;
    if (nOffset + s.mNRowStartHits >= GPUCA_MAX_TRACKLETS) {
      tracker.GPUParameters()->gpuError = GPUCA_ERROR_TRACKLET_OVERFLOW;
      CAMath::AtomicExch(tracker.NTracklets(), 0);
    }
#endif
  }
#endif
}
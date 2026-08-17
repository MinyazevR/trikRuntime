/* Copyright 2026 CyberTech Labs Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#pragma once

#include <trik/buffer.h>
#include <trik/sensors/cv_algorithm.h>
#include <trik/sensors/cv_algorithm_args.h>

#include "dspServer.h"

struct MessageQ_Object;
struct trik_msg;

namespace trikHal { class FbOutputInterface; }

namespace trikDsp {

/// TI IPC implementation (pimpl for DspServer).
///
/// ## Responsibility
///
/// Impl encapsulates all interaction with the TI IPC stack:
///   - MessageQ (host <-> slave queue pair for request/response)
///   - /dev/mem mmapping for DSP shared buffers
///   - Algorithm registration and frame processing
///
/// DspServer owns one Impl via QScopedPointer.
///
/// ## Initialisation sequence (called from DspServer::init)
///
///   startIpc()         -> Ipc_transportConfig + Ipc_start
///   setupMessageQueue() -> MessageQ_create (host) + retry-loop MessageQ_open (slave)
///   mapSharedBuffers()  -> INIT command over MessageQ, physToVirt x 2
///
/// If any step fails the object is left partially initialised.
/// ~Impl() safely tears down whatever was allocated:
///   destroyMessageQueue() -> null-safe (checks mHostQue / mSlaveQue)
///   Ipc_stop()            -> safe even without Ipc_start
///   munmap()              -> null-safe (checks mMmapIn/Out)
///
/// ## Concurrency contract
///
/// Impl has NO thread safety mechanisms of its own.
/// DspServer serialises access through the worker-thread event loop:
///   - addSource / removeSource -> BlockingQueuedConnection
///   - activate / deactivate     -> QueuedConnection
///   - onFrameReady              -> runs in worker thread
///
/// processFrame() and step() are called ONLY from onFrameReady,
/// so all Impl methods run in the worker thread.
///
/// ## MessageQ protocol
///
/// Every command follows the same pattern:
///   allocRequest(hostQue, heapId, size, cmd) -> sets cmd + reply queue
///   MessageQ_put(slaveQue, msg)
///   MessageQ_get(hostQue, ..., MessageQ_FOREVER) -> blocks until response
///   freeMessage(response)
///
/// MessageQ_FOREVER blocks the calling thread - there is no timeout.
/// This is acceptable because the DSP responds within microseconds
/// and a non-response indicates a DSP crash (fatal).
///
/// ## Resource ownership
///
///   mHostQue:   created by MessageQ_create  -> deleted in destroyMessageQueue
///   mSlaveQue:   opened by MessageQ_open     -> closed in destroyMessageQueue
///   mMmapIn/Out: allocated by mmap (physToVirt) -> freed by munmap in ~Impl()
///   mDspIn/Out:  point into mMmapIn/Out regions (not separate allocations)
class DspServer::Impl
{
public:
	/// Tears down IPC in reverse order:
	///   destroyMessageQueue -> Ipc_stop -> munmap(mMmapIn) -> munmap(mMmapOut)
	///
	/// All steps are null-safe - the object may be in any state
	/// (fully initialised, partially initialised, never initialised).
	~Impl();

	/// Configure the RPMsg transport and call Ipc_start().
	/// @return true on success.  On failure the caller should destroy.
	bool startIpc();

	/// Create the host MessageQ and open the slave queue.
	/// The slave queue may not be available immediately (DSP startup),
	/// so MessageQ_open is retried with 1-second sleeps (10 retries).
	/// @return true if both queues are ready, false on timeout/error.
	/// @note  Returns bool explicitly (was UB before fix).
	bool setupMessageQueue();

	/// Close the slave queue and delete the host queue.
	/// Safe to call regardless of initialisation state.
	void destroyMessageQueue();

	/// Send the INIT command to DSP, receive physical addresses of
	/// shared buffers, and mmap them via /dev/mem.
	///
	/// Stores mmap base pointers and lengths in mMmapIn/mMmapOut for
	/// proper munmap in the destructor.
	///
	/// @return true if both buffers were successfully mapped.
	bool mapSharedBuffers();

	/// Register a CV algorithm on the DSP.
	/// Sends the algorithm descriptor (pixel format, line length) via MessageQ.
	/// Must be called before the first step() for a new algorithm type.
	///
	/// @param algo  public API algorithm enum (converted to DSP command).
	/// @param desc  pixel format and bytes-per-line of the video source.
	void registerAlgorithm(Algorithm algo, const AlgoDescriptor &desc);

	/// Execute one frame-processing STEP on the DSP.
	/// Sends input args to DSP, blocks until response, copies output args.
	///
	/// @param in   sensor parameters (HSV ranges, matrix sizes, etc.).
	/// @param out  DSP processing results (locations, colours).
	/// @return true on success.
	bool step(const InArgs &in, OutArgs &out);

	/// Full frame processing pipeline:
	///   step() -> optionally fill videoFrame from mDspOut
	///
	/// @pre  The caller MUST have already copied the frame data into mDspIn
	///       (via the @c inBufferStart() / @c inBufferLen() helpers) before
	///       calling this method.
	/// @param channel     active channel (algorithm + inArgs + videoOut flag).
	/// @param out         filled with DSP results.
	/// @param videoFrame  if non-null and channel.videoOut == true,
	///                    filled with pointer into mDspOut (zero-copy).
	///                    The caller deep-copies for cross-thread signal emission.
	/// @return true if the frame was processed successfully.
	bool processFrame(const DspChannel &channel,
	                  OutArgs &out, VideoFrame *videoFrame = nullptr);

	/// Start of the DSP shared input buffer (mmap'd /dev/mem).
	void *inBufferStart() const { return mDspIn.start; }
	/// Length of the DSP shared input buffer.
	size_t inBufferLen() const { return mDspIn.length; }

	/// @name Active channel accessors (single-channel DSP)
	/// @{

	/// Set the active channel (replaces any previous one).
	void setChannel(const DspChannel &c) { mActive = c; }
	/// Clear the active channel (deactivate).
	void clearChannel() { mActive = {}; }

	/// Return a reference to the current channel.
	/// Safe to call when no channel is active (returns default-constructed).
	const DspChannel &channel() const { return mActive; }
	/// Return the algorithm of the active channel.
	Algorithm channelAlgo() const { return mActive.algorithm; }
	/// Return the sourceId of the active channel, or empty string if inactive.
	QString channelSourceId() const { return mActive.sourceId; }

	/// Clear the auto-detect flag on the active channel. This makes the flag a
	/// one-shot: the DSP detects on exactly the frame that carried
	/// auto_detect_hsv=true, and the host learns the detected range via
	/// resultReady() and feeds it back through a fresh activate().
	void consumeAutoDetect() { mActive.inArgs.autoDetect = false; }

	/// @}

	/// 	TI remoteproc ID of the DSP core (0 for OMAP-L138).
	uint16_t rprocId = 0;

	/// HAL framebuffer output (optional, set via setFbOutput).
	class trikHal::FbOutputInterface *mFbOutput = nullptr;

private:
	/// Send a MessageQ request and block until the DSP responds.
	/// @return the response message, or nullptr on failure.
	/// @note  Blocks with MessageQ_FOREVER - no timeout.
	::trik_msg *sendAndWaitForResponse(::trik_msg *msg);

	/// Free a MessageQ-allocated message.
	static void freeMessage(::trik_msg *msg);

	/// Currently active DSP channel.
	/// videoOut=false / source=nullptr when inactive.
	DspChannel mActive;

	/// DSP shared input buffer (mmap'd /dev/mem).
	/// data.start -> usable data pointer (page-offset into mmap region).
	/// data.length -> BUFFER_SIZE.
	struct buffer mDspIn = {};

#ifndef TRIK_DSP_STUB
	/// Last registered algorithm.  Used to avoid re-registration in processFrame.
	enum trik_cv_algorithm mCurrentAlgo = TRIK_CV_ALGORITHM_NONE;

	/// TI IPC host queue handle (created by MessageQ_create).
	struct MessageQ_Object *mHostQue = nullptr;

	/// TI IPC slave queue handle (opened by MessageQ_open).
	/// Initialised to MessageQ_INVALIDMESSAGEQ (0xffff).
	unsigned mSlaveQue = 0xffff;

	/// DSP shared output buffer (mmap'd /dev/mem).
	struct buffer mDspOut = {};

	/// mmap base address for mDspIn (page-aligned, for munmap).
	void *mMmapIn = nullptr;
	size_t mMmapInLen = 0;

	/// mmap base address for mDspOut (page-aligned, for munmap).
	void *mMmapOut = nullptr;
	size_t mMmapOutLen = 0;
#endif
};

}

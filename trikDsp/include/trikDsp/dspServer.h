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

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QScopedPointer>

#include "dspSource.h"
#include "dspTypes.h"
#include "trikDspDeclSpec.h"

namespace trikHal { class FbOutputInterface; }

namespace trikDsp {

/// ARM <-> DSP bridge via TI IPC MessageQ over RPMsg.
///
/// ## Lifecycle & thread model
///
/// Construction is cheap - only the Impl is heap-allocated, no IPC is started.
/// Caller MUST call init() to bring up the IPC stack (LAD daemon -> Ipc_start ->
/// MessageQ -> shared buffers).  init() is a **synchronous blocking** call that
/// spins a local QEventLoop; the calling thread's event loop keeps running
/// during the wait.  When init() returns the object has either emitted
/// successfullyInited() (ready for use) or errorOccurred() (permanent failure).
///
/// After successful init, the caller should call moveToThread() if a dedicated
/// worker thread is desired.  The object is designed **without thread
/// ownership** - the caller creates and manages the QThread.
///
/// Destructor terminates the LAD daemon (terminate() -> kill() fallback).
/// Impl::~Impl() tears down IPC (destroyMessageQueue -> Ipc_stop -> munmap).
///
/// ## Single-channel design
///
/// DspServer processes frames from one active channel at a time.
/// When a new channel is activated the old one is silently replaced.
/// Frames from non-active sources are dropped.
///
/// ## Video display
///
/// Video output goes through HAL FbOutputInterface (no Qt signals in hot path):
/// activate() opens the fb when the channel has videoOut=true, deactivate()
/// closes it.
///
/// ## Concurrency
///
/// - init(): runs in caller's thread, blocking.
/// - activate() / deactivate(): QueuedConnection - non-blocking.
/// - processFrameData(): MUST be called from DspServer's thread via
///   QMetaObject::invokeMethod with Qt::QueuedConnection.
/// - mLadProcess: value member.  Started in init() (caller's thread), stays in
///   that thread regardless of moveToThread().  terminate() is thread-safe.
class TRIKDSP_EXPORT DspServer : public QObject
{
	Q_OBJECT

public:
	/// Constructs the object WITHOUT IPC initialisation.
	/// @param rprocId  remoteproc ID of the DSP core (0 for OMAP-L138).
	/// @param parent   optional QObject parent.
	///
	/// @post  mLadProcess default-constructed (unstarted).
	/// @post  d->rprocId set, nothing else allocated.
	explicit DspServer(uint16_t rprocId, QObject *parent = nullptr);

	/// Terminates the LAD daemon and destroys IPC resources.
	~DspServer() override;

	/// Synchronously bring up the IPC stack.  BLOCKING.
	///
	/// Sequence: start lad_omapl138 (QProcess) -> wait up to 15s for start ->
	/// Ipc_start() -> setupMessageQueue() -> mapSharedBuffers().
	///
	/// Emits errorOccurred(msg) on failure, successfullyInited() on success.
	///
	/// @warning Caller MUST connect to errorOccurred and successfullyInited
	///          BEFORE calling init().  These signals fire synchronously during
	///          this call.
	void init();

	/// Activate a DSP channel.  Non-blocking (QueuedConnection).
	///
	/// Replaces the current active channel.  Opens the framebuffer when the
	/// new channel has videoOut=true (closes it first if a previous channel
	/// had it open with a different algorithm).
	///
	/// Thread-safe - can be called from any thread.
	void activate(const DspChannel &channel);

	/// Deactivate the current channel.  Non-blocking (QueuedConnection).
	///
	/// Clears the active channel and closes the framebuffer if it was open.
	/// Idempotent.
	///
	/// Thread-safe - can be called from any thread.
	void deactivate();

	/// Process the frame captured into the DSP input buffer @p bufferIdx.
	/// The DSP reads the frame directly from that buffer (no host-side copy).
	/// MUST be called from the DspServer thread (use invokeMethod).
	///
	/// Drops frames from non-active sources (but still emits resultReady so the
	/// caller can release the V4L2 buffer).
	/// On success, emits resultReady() and writes video to FbOutput if attached.
	Q_INVOKABLE void processFrameData(const QString &sourceId, uint32_t bufferIdx);

	/// Attach a HAL framebuffer output for direct video display.
	/// Must be called before activate with videoOut=true.
	/// DspServer takes ownership.
	void setFbOutput(trikHal::FbOutputInterface *fb);

Q_SIGNALS:
	/// @name DSP processing signals
	/// @{

	/// Emitted from the worker thread after each successfully processed frame.
	/// OutArgs is passed by const reference to avoid copying the JPEG payload.
	/// @p bufferIdx identifies the capture buffer whose frame was processed;
	/// the caller must return that buffer to the driver when it sees this signal.
	void resultReady(const QString &sourceId,
			 trikDsp::Algorithm algorithm,
			 const trikDsp::OutArgs &result,
			 uint32_t bufferIdx);

	/// @}

	/// @name Lifecycle signals
	/// @{

	/// Emitted synchronously from init() when IPC initialisation fails.
	void errorOccurred(const QString &message);

	/// Emitted synchronously from init() when IPC initialisation succeeds.
	void successfullyInited();

	/// @}

private:
	class Impl;
	QScopedPointer<Impl> d;

	QProcess mLadProcess;
};

}

#pragma once

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QScopedPointer>

#include "dspSource.h"
#include "dspTypes.h"
#include "trikDspDeclSpec.h"

namespace trikHal { class FbOutputInterface; }

namespace trikDsp {

/// ARM ↔ DSP bridge via TI IPC MessageQ over RPMsg.
///
/// ## Lifecycle & thread model
///
/// Construction is cheap — only the Impl is heap-allocated, no IPC is started.
/// Caller MUST call init() to bring up the IPC stack (LAD daemon → Ipc_start →
/// MessageQ → shared buffers).  init() is a **synchronous blocking** call that
/// spins a local QEventLoop; the calling thread's event loop keeps running
/// during the wait.  When init() returns the object has either emitted
/// successfullyInited() (ready for use) or errorOccurred() (permanent failure).
///
/// After successful init, the caller should call moveToThread() if a dedicated
/// worker thread is desired.  The object is designed **without thread
/// ownership** — the caller creates and manages the QThread.
///
/// Destructor terminates the LAD daemon (terminate() → kill() fallback).
/// Impl::~Impl() tears down IPC (destroyMessageQueue → Ipc_stop → munmap).
///
/// ## Single-channel design
///
/// DspServer processes frames from one active channel at a time.
/// When a new channel is activated the old one is silently replaced.
/// Frames from non-active sources are dropped.
///
/// ## Signal contract for video display
///
/// activate() emits videoDisplayFinished() for the previous channel if it had
/// videoOut=true, then emits videoDisplayStarted() if the new channel has
/// videoOut=true.  deactivate() emits videoDisplayFinished() once.
/// Video output goes through HAL FbOutputInterface (no Qt signals in hot path).
///
/// ## Concurrency
///
/// - init(): runs in caller's thread, blocking.
/// - activate() / deactivate(): QueuedConnection — non-blocking.
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
	/// Sequence: start lad_omapl138 (QProcess) → wait up to 15s for start →
	/// Ipc_start() → setupMessageQueue() → mapSharedBuffers().
	///
	/// Emits errorOccurred(msg) on failure, successfullyInited() on success.
	///
	/// @warning Caller MUST connect to errorOccurred and successfullyInited
	///          BEFORE calling init().  These signals fire synchronously during
	///          this call.
	void init();

	/// Activate a DSP channel.  Non-blocking (QueuedConnection).
	///
	/// Replaces the current active channel.  If the previous channel had
	/// videoOut=true, emits videoDisplayFinished() first.  If the new channel
	/// has videoOut=true, emits videoDisplayStarted().
	///
	/// Thread-safe — can be called from any thread.
	void activate(const DspChannel &channel);

	/// Deactivate the current channel.  Non-blocking (QueuedConnection).
	///
	/// Clears the active channel.  If it had videoOut=true, emits
	/// videoDisplayFinished().  Idempotent.
	///
	/// Thread-safe — can be called from any thread.
	void deactivate();

	/// Copy a video frame into the DSP shared input buffer.
	///
	/// This is a plain (non-thread-affine) method: it may be called from the
	/// streaming consumer's thread (main). It is serialized with the DSP-side
	/// processing by the frame flow — releaseFrame is deferred until
	/// resultReady, so the shared buffer is never accessed concurrently.
	void copyFrame(const uint8_t *data, size_t size);

	/// Process the frame previously copied by copyFrame().
	/// MUST be called from the DspServer thread (use invokeMethod).
	///
	/// Drops frames from non-active sources (but still emits resultReady so the
	/// caller can release the V4L2 buffer).
	/// On success, emits resultReady() and writes video to FbOutput if attached.
	Q_INVOKABLE void processFrameData(const QString &sourceId);

	/// Attach a HAL framebuffer output for direct video display.
	/// Must be called before activate with videoOut=true.
	/// DspServer takes ownership.
	void setFbOutput(trikHal::FbOutputInterface *fb);

Q_SIGNALS:
	/// @name DSP processing signals
	/// @{

	/// Emitted from the worker thread after each successfully processed frame.
	void resultReady(const QString &sourceId,
			 trikDsp::Algorithm algorithm,
			 trikDsp::OutArgs result);

	/// Emitted when a channel with videoOut=true is deactivated or replaced.
	void videoDisplayFinished();

	/// Emitted when a channel with videoOut=true is activated.
	void videoDisplayStarted();

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

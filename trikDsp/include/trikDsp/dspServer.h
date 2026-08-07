#pragma once

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QScopedPointer>

#include "dspSource.h"
#include "dspTypes.h"
#include "trikDspDeclSpec.h"

namespace trikHal {
class VideoDeviceFileInterface;
}

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
/// DspServer processes frames for ALL registered sources, but only ONE channel
/// (source + algorithm pair) is active at a time.  When a new channel is
/// activated the old one is silently replaced.  Frames from non-active sources
/// are ignored (source != channelSource check in onFrameReady).
///
/// ## Signal contract for video display
///
/// activate() emits videoDisplayFinished() for the previous channel if it had
/// videoOut=true, then emits videoDisplayStarted() if the new channel has
/// videoOut=true.  deactivate() emits videoDisplayFinished() once.
/// videoFrameReady() carries a deep-copied QByteArray of the DSP output buffer
/// (RGB565) — safe across thread boundaries.
///
/// ## Ownership
///
/// DspServer does NOT own video sources — the caller is responsible for their
/// lifetime.  addSource() starts streaming and subscribes to frameReady();
/// removeSource() stops streaming, closes and unsubscribes.  The source must
/// outlive its registration on DspServer.
///
/// ## Concurrency
///
/// - addSource() / removeSource(): BlockingQueuedConnection — blocks caller
///   until the DspServer thread processes the request.  DspServer MUST live in
///   a thread with a running event loop.
/// - activate() / deactivate(): QueuedConnection — non-blocking.
/// - onFrameReady(): runs in the DspServer thread.  Frame signals from sources
///   travel via queued connection (AutoConnection across threads).
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
	///
	/// If init() was called: mLadProcess.terminate() (3s) → kill() (1s).
	/// If init() was never called: terminate() is a no-op (process not started).
	///
	/// Impl destructor always calls destroyMessageQueue() → Ipc_stop() →
	/// munmap().  All three are safe with partial/zero initialisation.
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
	///          this call.  Do NOT destroy DspServer from inside errorOccurred
	///          — init() is still on the stack.
	///
	/// @post If successful: DSP is ready, shared buffers are mapped, MessageQ
	///       is open. If failed: nothing allocated (all partial resources are
	///       cleaned up in ~Impl()).
	void init();

	/// Register a video source.  BLOCKS the calling thread (BlockingQueued).
	///
	/// - Calls source->startStreaming() — source must already be open()'d.
	/// - Connects source->frameReady to internal onFrameReady slot.
	///
	/// @return true on success, false if startStreaming failed.
	/// @note  Idempotency: NOT guaranteed.  Calling addSource twice on the same
	///        source results in duplicate frameReady connections.  Caller
	///        (VideoSensorManager) must guard against this.
	bool addSource(trikHal::VideoDeviceFileInterface *source);

	/// Unregister a video source.  BLOCKS the calling thread (BlockingQueued).
	///
	/// - Disconnects ALL frameReady signals from this source to DspServer.
	/// - Calls source->stopStreaming() then source->close().
	/// - The source pointer is NOT deleted — caller owns it.
	///
	/// @note  Safe to call on a source that was never added (no-op).
	///        Safe to call on a source that is already stopped/closed.
	void removeSource(trikHal::VideoDeviceFileInterface *source);

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
	/// videoDisplayFinished().  Idempotent — repeated calls are no-ops.
	///
	/// Thread-safe — can be called from any thread.
	void deactivate();

Q_SIGNALS:
	/// @name DSP processing signals
	/// @{

	/// Emitted from the worker thread after each successfully processed frame.
	/// @param sourceId  unique identifier of the video source (device path).
	/// @param algorithm the algorithm that produced this result.
	/// @param result    processed output (coordinates, detected colours, etc.).
	void resultReady(const QString &sourceId,
	                 Algorithm algorithm,
	                 OutArgs result);

	/// Emitted when videoOut=true after DSP processing.
	/// Carries a deep-copied QByteArray of the DSP output buffer (RGB565),
	/// safe to pass across thread boundaries.
	void videoFrameReady(const QByteArray &data,
	                     uint32_t width, uint32_t height);

	/// Emitted when a channel with videoOut=true is deactivated or replaced.
	/// Consumer (trikGui) should clear the display.
	void videoDisplayFinished();

	/// Emitted when a channel with videoOut=true is activated.
	/// Consumer (trikGui) should prepare the display for DSP frames.
	void videoDisplayStarted();

	/// @}

	/// @name Lifecycle signals
	/// @{

	/// Emitted synchronously from init() when IPC initialisation fails.
	/// The object is in a permanently failed state — destroy it.
	void errorOccurred(const QString &message);

	/// Emitted synchronously from init() when IPC initialisation succeeds.
	/// The object is ready for addSource/activate/deactivate.
	void successfullyInited();

	/// @}

private Q_SLOTS:
	void onFrameReady();

private:
	class Impl;
	QScopedPointer<Impl> d;

	/// QProcess for the LAD daemon (lad_omapl138).  Value member — thread
	/// affinity follows the thread where init() is called.  Not moved by
	/// moveToThread() (not a parented child).  terminate() in destructor
	/// is called from the caller's thread regardless.
	QProcess mLadProcess;
};

}

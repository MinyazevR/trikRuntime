#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QReadWriteLock>
#include <QtCore/QSharedPointer>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <QtCore/QVector>
#include <functional>
#include <utility>

#include <trikKernel/videoUtils.h>

namespace trikHal {
class HardwareAbstractionInterface;
class VideoDeviceFileInterface;
}
namespace trikKernel { class Configurer; }

namespace trikControl {

/// Shared camera device manager with refcounting.
///
/// Owns the underlying V4L2 devices (trikHal::VideoDeviceFileInterface) and
/// lets several clients use the same camera at once:
///   - VideoSensorManager streams frames into the DSP,
///   - V4l2CameraImplementation grabs a single photo.
///
/// The configuration is read exactly once, in the constructor. For every port
/// of class "videoSensor" a static device state is stored (device file path,
/// width/height/fourcc and a `ready` flag). No method touches the Configurer
/// afterwards: each one looks the port up in the internal map and rejects the
/// call if the stored state is not `ready`.
///
/// ## Sharing model (refcount)
/// `acquire()` increments the refcount and opens+starts the device on the very
/// first acquisition; every following acquire() just bumps the counter. A
/// client must call `release()` when it no longer needs the camera. When the
/// refcount drops to zero the device is stopped, closed and destroyed, and all
/// pending subscriptions are dropped.
///
/// ## Push vs pull consumers (explicit, decoupled)
/// Two consumer kinds:
///   - Push (streaming) subscriber — at most ONE per port, registered with
///     subscribe(). Its callback runs synchronously in the manager's thread on
///     every frame, receives the raw zero-copy pointer (valid only during the
///     callback), and OWNS the buffer release: it must call releaseFrame() once
///     it has consumed the frame. That release is the backpressure point (e.g.
///     the DSP worker decides how fast the stream advances). A callback MUST be
///     lightweight: copy the frame where needed (e.g. the DSP shared input
///     buffer) and, if required, queue async processing (DspServer::
///     processFrameData).
///   - Pull (latest-frame) clients — never observe the raw buffer and never
///     hold it. While at least one is registered (subscribeLatest), every frame
///     is latched into a private ref-counted copy and grabLatestFrame() returns
///     the freshest one to any client in any thread, at its own pace.
///
/// The V4L2 buffer is returned to the driver via releaseFrame(). If a streaming
/// subscriber is registered it owns that release (deferred); otherwise
/// onFrameReady() releases the buffer right after delivery so the stream keeps
/// flowing for pull clients. The device lives in the manager's thread; all
/// device-touching operations are marshalled there, so the QSocketNotifier stays
/// in a deterministic thread regardless of which thread acquires first.
///
/// ## Thread safety
/// mDevices and mLatest are guarded by an internal QReadWriteLock (getters may
/// be called from any thread). Subscription maps (mStreamingSubs / mPullSubs)
/// are mutated only in the manager's thread (subscribe*/unsubscribe* are
/// marshalled there), so the capture hot path onFrameReady() reads them without
/// locking.
class CameraManager : public QObject
{
	Q_OBJECT

public:
	/// Frame callback: (raw frame data, size in bytes).
	using FrameCb = std::function<void(const uint8_t *data, size_t size)>;

	explicit CameraManager(const trikKernel::Configurer &configurer,
	                       const trikHal::HardwareAbstractionInterface &hal,
	                       QObject *parent = nullptr);
	~CameraManager() override;

	/// Acquire the camera on @p port using the format recorded in the config.
	/// On the first acquisition the device is opened and streaming is started
	/// with the port's static format; on subsequent ones only the refcount is
	/// incremented.
	/// @return false if the port is unknown or its stored state is not `ready`.
	bool acquire(const QString &port);

	/// Whether @p port is currently held by at least one client.
	bool isOccupied(const QString &port) const;

	/// Register the single streaming (push) subscriber of @p port.
	///
	/// The callback runs synchronously in the manager's thread on every frame
	/// with the raw zero-copy pointer (valid only during the call) and owns the
	/// buffer release: it must call releaseFrame() once it has consumed the
	/// frame. At most one streaming subscriber per port; re-subscribing replaces
	/// the previous one. The port must already be acquired.
	void subscribe(const QString &port, QObject *receiver, FrameCb callback);

	/// Remove a previously registered subscription of @p receiver from @p port.
	void unsubscribe(const QString &port, QObject *receiver);

	/// Register @p receiver as a "latest frame" (pull) client of @p port.
	///
	/// While at least one pull client is registered, the manager latches every
	/// captured frame into a private copy (one extra memcpy per frame) and emits
	/// latestFrameReady(). Pull clients never hold the V4L2 buffer, so they do
	/// not delay release and never observe the raw mmap buffer. The port must
	/// already be acquired.
	void subscribeLatest(const QString &port, QObject *receiver);

	/// Remove a previously registered pull client of @p receiver from @p port.
	void unsubscribeLatest(const QString &port, QObject *receiver);

	/// Return the most recently latched frame of @p port, or nullptr if none
	/// was latched yet. The returned buffer is ref-counted and stays valid
	/// after this call, so it may be used from any thread.
	QSharedPointer<QByteArray> grabLatestFrame(const QString &port) const;

	/// Pause streaming on @p port without releasing the device.
	void stopStreaming(const QString &port);

	/// Resume streaming after stopStreaming().
	void startStreaming(const QString &port);

	/// Decrement the refcount of @p port. When it reaches zero the device is
	/// stopped, closed, destroyed and every subscription is dropped.
	void release(const QString &port);

	/// Return the currently held V4L2 buffer back to the driver (QBUF), so the
	/// next frame can be captured. Called by the streaming consumer once per
	/// frame, after it has copied the data.
	void releaseFrame(const QString &port);

	/// Configured (static) format getters, used e.g. to set up the DSP.
	uint32_t width(const QString &port) const;
	uint32_t height(const QString &port) const;
	uint32_t fourcc(const QString &port) const;

	/// Actual (negotiated) pixel format of @p port, cached at acquire time.
	trikKernel::PixelFormat format(const QString &port) const;

	/// Actual bytes-per-line (V4L2 bytesperline) of @p port, cached at acquire time.
	uint32_t lineLength(const QString &port) const;

	/// Whether the config for @p port is valid (the port exists and its state
	/// was recorded as ready in the constructor).
	bool isReady(const QString &port) const;

	/// Device file path (e.g. "/dev/video0") the @p port is bound to. Empty if
	/// the port is unknown or its stored state is not `ready`.
	QString deviceFile(const QString &port) const;

Q_SIGNALS:
	/// Emitted after a new frame has been latched for @p port (see
	/// subscribeLatest()). The buffer is available via grabLatestFrame().
	void latestFrameReady(const QString &port);

private:
	/// Static per-port state recorded once in the constructor.
	struct Entry {
		trikHal::VideoDeviceFileInterface *dev = nullptr;
		QString devFile;                 ///< Path to /dev/videoN.
		uint32_t w = 0, h = 0, fmt = 0;  ///< Format from the config.
		trikKernel::PixelFormat format = trikKernel::PixelFormat::Unknown;
		uint32_t lineLength = 0;
		int refCount = 0;                ///< Number of active clients.
		bool streaming = false;          ///< Whether streaming is currently on.
		bool ready = false;              ///< Config for the port is valid.
	};

	/// The single streaming (push) subscriber of a port: the receiver (for
	/// liveness and unsubscribe identity) and the callback.
	struct StreamingSub {
		QPointer<QObject> receiver;
		FrameCb callback;
	};

	/// Opens (or reuses) the device of @p entry using its static config format
	/// and bumps its refcount. Assumes the write lock is already held and the
	/// call is running in the manager's thread.
	bool openDeviceLocked(const QString &port, Entry &entry);

	/// Fans out a captured frame to all subscribers of @p port. Runs in the
	/// manager's thread (direct connection from the device notifier).
	void onFrameReady(const QString &port, const uint8_t *data, size_t size);

	/// Drop dead pull clients of @p port. Runs in the manager's thread, invoked
	/// by each pull receiver's destroyed() handler (see subscribeLatest), so the
	/// capture hot path never traverses the pull list.
	void prunePullSubs(const QString &port);

	/// Runs @p fn in the manager's thread, blocking the caller if it lives in
	/// another thread.
	template <typename Fn>
	void runInManagerThread(Fn &&fn)
	{
		if (QThread::currentThread() == thread()) {
			fn();
			return;
		}
		QMetaObject::invokeMethod(this, std::forward<Fn>(fn), Qt::BlockingQueuedConnection);
	}

	/// Like runInManagerThread() but returns the value produced by @p fn.
	template <typename T, typename Fn>
	T callInManagerThread(Fn &&fn)
	{
		if (QThread::currentThread() == thread()) {
			return fn();
		}
		T result{};
		QMetaObject::invokeMethod(this, [&result, &fn]() { result = fn(); },
		                          Qt::BlockingQueuedConnection);
		return result;
	}

	const trikHal::HardwareAbstractionInterface &mHal;

	/// Port name -> device state. Filled once in the constructor.
	QHash<QString, Entry> mDevices;

	/// Port name -> the streaming (push) subscriber (at most one).
	QHash<QString, StreamingSub> mStreamingSubs;

	/// Port name -> registered pull clients (QPointer for liveness).
	QHash<QString, QVector<QPointer<QObject>>> mPullSubs;

	/// Port name -> most recently latched frame (ref-counted).
	QHash<QString, QSharedPointer<QByteArray>> mLatest;

	mutable QReadWriteLock mLock;
};

} // namespace trikControl

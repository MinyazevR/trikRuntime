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

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>
#include <QtCore/QScopedPointer>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include <trikKernel/videoUtils.h>
#include <trikHal/physicalMemoryMapper.h>

namespace trikHal {
class HardwareAbstractionInterface;
class VideoDeviceFileInterface;
}
namespace trikKernel { class Configurer; }

namespace trikControl {

/// std::hash adapter so std::unordered_map can be keyed by a QString port name.
struct QStringHash
{
	std::size_t operator()(const QString &key) const noexcept
	{
		return static_cast<std::size_t>(qHash(key));
	}
};

/// Shared camera device manager with refcounting.
///
/// Owns the underlying V4L2 devices (trikHal::VideoDeviceFileInterface) and
/// lets several clients use the same camera at once:
///   - VideoSensorManager streams frames into the DSP,
///   - V4l2CameraImplementation grabs a single photo.
///
/// The configuration is read exactly once, in the constructor. For every port
/// of class "videoDevice" a static device state is stored (device file path,
/// width/height/fourcc and a `ready` flag). No method touches the Configurer
/// afterwards: each one looks the port up in the internal map and rejects the
/// call if the stored state is not `ready`.
///
/// ## Sharing model (refcount)
/// `acquire()` increments the refcount and opens+starts the device on the very
/// first acquisition; every following acquire() just bumps the counter. A
/// client must call `release()` when it no longer needs the camera. When the
/// refcount drops to zero the device is stopped, closed and destroyed.
///
/// ## Frame delivery (zero-copy)
/// Frames are delivered as a `frameReady(port, bufferIdx, data, size)` signal.
/// The raw `data` pointer (an mmap'd V4L2 buffer, or a capture buffer in
/// USERPTR mode) stays valid until the buffer is returned to the driver.
///
/// Every buffer index carries a per-port reference count. A client that wants
/// to keep a delivered frame past the signal return claims it with
/// `retainFrame(port, bufferIdx)` (refcount++) and hands it back with
/// `releaseFrame(port, bufferIdx)` (refcount--). When the refcount reaches
/// zero the buffer is queued back to the driver (QBUF) so the next frame can
/// be captured into it. All clients are equal: several of them may retain the
/// same frame and the buffer is only recycled once the last one releases it.
///
/// The whole counter lives in the manager's worker thread, so the hand-off of
/// the current buffer index is atomic and no extra locking is needed. In
/// USERPTR mode the VPIF DMA engine writes straight into the capture buffers,
/// so the DSP reads the frame without a host-side copy.
///
/// The device lives in the manager's own worker thread; all device-touching
/// operations are marshalled there, so the QSocketNotifier stays in a
/// deterministic thread regardless of which thread acquires first.
class CameraManager : public QObject
{
	Q_OBJECT

public:
	explicit CameraManager(const trikKernel::Configurer &configurer,
	                       const trikHal::HardwareAbstractionInterface &hal,
	                       QObject *parent = nullptr);
	~CameraManager() override;

	/// Acquire the camera on @p port using the format recorded in the config.
	/// On the first acquisition the device is opened and streaming is started;
	/// on subsequent ones only the refcount is incremented. Asynchronous: the
	/// outcome is reported via acquired().
	///
	/// The device is opened in USERPTR mode capturing straight into the port's
	/// capture region (zero-copy): the region is assigned in the constructor and
	/// mapped up front, so this works even before the DSP is initialised. When
	/// the capture region is unavailable the device falls back to
	/// driver-allocated MMAP buffers.
	void acquire(const QString &port);

	/// Decrement the refcount of @p port. When it reaches zero the device is
	/// stopped, closed and destroyed - unless a stopStreaming() was requested,
	/// in which case the camera is only parked (streamoff) and stays open for a
	/// quick re-acquire().
	void release(const QString &port);

	/// Ask to park the camera on @p port: the device stays acquired/open but the
	/// stream is stopped once the last client releases the camera (or right away
	/// if it is the only client). A later acquire() starts it again.
	void stopStreaming(const QString &port);

	/// Forcibly tear down all cameras, regardless of the current refcounts.
	/// Runs in the manager's thread and blocks until done. A subsequent
	/// acquire() opens the device again from its static config.
	void stop();

	/// Forcibly tear down the camera on @p port only, regardless of its
	/// refcount. Blocks until done. A later acquire() reopens the device.
	void stop(const QString &port);

	/// Claim the frame delivered as @p bufferIdx of @p port for one additional
	/// client, incrementing its reference count. The buffer is not returned to
	/// the driver until every claim (including this one) is dropped via
	/// releaseFrame(). A delivered frame that no client claims is automatically
	/// returned to the driver when the next frame of the port arrives. Pair
	/// retainFrame() with a matching releaseFrame() once the client has consumed
	/// the frame.
	void retainFrame(const QString &port, uint32_t bufferIdx);

	/// Drop one reference to the frame @p bufferIdx of @p port. When the
	/// reference count reaches zero the buffer is queued back to the driver
	/// (QBUF), so the next frame can be captured into it.
	void releaseFrame(const QString &port, uint32_t bufferIdx);

	/// Number of capture buffers in one port region.
	uint32_t inputBuffersPerRegion() const;

	/// Length of a single capture buffer in bytes.
	size_t inputBufferLen() const;

	/// Capture region index (0..regions-1) assigned to @p port, or 0 if the
	/// port has not been acquired for streaming.
	uint32_t inputRegion(const QString &port) const;

	/// Configured (static) format getters, used e.g. to set up the DSP.
	uint32_t width(const QString &port) const;
	uint32_t height(const QString &port) const;
	uint32_t fourcc(const QString &port) const;

	/// Actual (negotiated) pixel format of @p port, cached at acquire time.
	trikKernel::PixelFormat format(const QString &port) const;

	/// Actual bytes-per-line (V4L2 bytesperline) of @p port, cached at acquire time.
	uint32_t lineLength(const QString &port) const;

	/// Device file path (e.g. "/dev/video0") the @p port is bound to. Empty if
	/// the port is unknown or its stored state is not `ready`.
	QString deviceFile(const QString &port) const;

	/// mjpg-streamer launcher script path recorded for @p port (empty if the
	/// port is unknown or has no "mjpgStreamerScript" configured).
	QString streamerScript(const QString &port) const;

Q_SIGNALS:
	/// Emitted (in the worker thread) when a frame is captured into buffer
	/// @p bufferIdx of @p port. @p data is the mmap'd virtual address of the
	/// buffer and @p size the frame size in bytes. The buffer stays valid while
	/// a client holds a reference to it (see retainFrame()) and must not be
	/// touched after that client's matching releaseFrame().
	void frameReady(const QString &port, uint32_t bufferIdx,
	                const uint8_t *data, size_t size);

	/// Emitted (in the worker thread) when a previously requested acquire() of
	/// @p port has finished. @p ok is false when the port is unknown, its
	/// stored state is not `ready`, or the device failed to open.
	void acquired(const QString &port, bool ok);

private:
	/// Static per-port state recorded once in the constructor.
	struct Entry {
		std::unique_ptr<trikHal::VideoDeviceFileInterface> dev;
		QString devFile;                 ///< Path to /dev/videoN.
		QString mjpgStreamerScript;      ///< mjpg-streamer launcher script path.
		uint32_t w = 0, h = 0, fmt = 0;  ///< Format from the config.
		trikKernel::PixelFormat format = trikKernel::PixelFormat::Unknown;
		uint32_t lineLength = 0;
		int refCount = 0;                ///< Number of active clients.
		bool stopRequested = false;      ///< Client asked to park the camera.
		bool streaming = false;          ///< Whether streaming is currently on.
		bool ready = false;              ///< Config for the port is valid.
		/// Reference count per delivered-but-not-yet-recycled buffer index. A
		/// frame is delivered with count 0; clients raise it with retainFrame()
		/// and drop it with releaseFrame(); the buffer is QBUF'd at zero.
		QHash<uint32_t, uint32_t> frameRefCount;
		int i2cBus = 0;                  ///< I2C bus (ov7670 analog ports only).
		int i2cAddress = 0;              ///< I2C address (ov7670 analog ports only).
		int gpioNumber = 0;              ///< Reset GPIO (ov7670 analog ports only).
		bool sensorInitialized = false;  ///< ov7670 already configured once, so
		                                 ///< a re-open (hot-plug) skips init.
	};

	/// Opens (or reuses) the device of @p entry using its static config format
	/// and bumps its refcount. When the capture region is mapped the device is
	/// configured in USERPTR mode capturing into the port's region (see
	/// acquire()); otherwise it falls back to MMAP. Assumes the write lock is
	/// already held and the call is running in the manager's thread.
	bool openDeviceLocked(const QString &port, Entry &entry);

	/// Relays a frame captured by the device of @p port to the frameReady()
	/// signal. Runs in the manager's thread (direct connection from the device).
	void onDeviceFrame(const QString &port, uint32_t bufferIdx,
	                   const uint8_t *data, size_t size);

	/// Map the capture region (fixed physical address) into host virtual memory
	/// via /dev/mem, so the VPIF DMA engine can capture straight into it without
	/// waiting for the DSP's INIT response. Runs in the constructor. Returns
	/// true on success.
	bool mapInputRegion();

	/// Stop, close and destroy the open device of a single @p port, reset its
	/// refcount/streaming flags. Assumes the write lock is already held and the
	/// call is running in the manager's thread. The static per-port config is
	/// left intact so a later acquire() can re-open the device.
	void tearDownPortLocked(const QString &port, Entry &entry);

	/// Stop, close and destroy every open device. Assumes the write lock is
	/// already held and the call is running in the manager's thread.
	void tearDownLocked();

	/// Eagerly initialize every ov7670 analog port (identified by a configured
	/// I2C bus). Posted to the manager's thread from the constructor, so the
	/// slow sensor bring-up happens off the GUI thread, before the first
	/// acquire(). initVideoSensor() returns false when no sensor is physically
	/// wired, so an absent camera simply stays uninitialized and is retried on
	/// acquire().
	void initSensors();

	/// Runs @p fn in the manager's thread, blocking the caller if it lives in
	/// another thread. Used only by the destructor and stop(), which must not
	/// return before the devices are torn down in their own thread.
	template <typename Fn>
	void runInManagerThread(Fn &&fn)
	{
		if (QThread::currentThread() == thread()) {
			fn();
			return;
		}
		QMetaObject::invokeMethod(this, std::forward<Fn>(fn), Qt::BlockingQueuedConnection);
	}

	/// Runs @p fn in the manager's thread. When called from another thread the
	/// call is posted (non-blocking); when already in the manager's thread it
	/// runs directly. The manager's event queue preserves the order of all
	/// posted operations, which is what makes acquire/release/refcount races safe.
	template <typename Fn>
	void runAsync(Fn &&fn)
	{
		if (QThread::currentThread() == thread()) {
			fn();
			return;
		}
		QMetaObject::invokeMethod(this, std::forward<Fn>(fn), Qt::QueuedConnection);
	}

	const trikHal::HardwareAbstractionInterface &mHal;

	/// Worker thread the manager and its devices live on.
	QScopedPointer<QThread> mThread;

	/// Port name -> device state. Filled once in the constructor. Uses
	/// std::unordered_map because Entry is move-only (it owns the device via a
	/// unique_ptr), which Qt's QHash (copy-based) does not support.
	std::unordered_map<QString, Entry, QStringHash> mDevices;

	/// RAII-mapped capture region (mmap'd /dev/mem). Written once in the
	/// constructor, then read-only; munmaps itself on destruction.
	trikHal::MappedMemory mInputMap;

	/// Capture region index assigned to each video port. Static: filled once in
	/// the constructor, never changed afterwards.
	QHash<QString, uint32_t> mPortRegions;

	mutable QReadWriteLock mLock;
};

} // namespace trikControl

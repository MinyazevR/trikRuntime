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

#include "cameraManager.h"

#include <QtCore/QByteArray>

#include <trikHal/VideoDeviceFileInterface.h>
#include <trikHal/hardwareAbstractionInterface.h>
#include <trikKernel/configurer.h>
#include <trikKernel/videoUtils.h>
#include <trikDsp/dspTypes.h>
#include <QsLog.h>

#include "configurerHelper.h"
#include "deviceState.h"

namespace trikControl {

CameraManager::CameraManager(const trikKernel::Configurer &configurer,
                             const trikHal::HardwareAbstractionInterface &hal,
                             QObject *parent)
	: QObject(parent)
	, mHal(hal)
{
	// Single pass over the config: record the static state of every video
	// sensor port. The Configurer is never touched after this loop - all
	// later lookups go through mDevices.
	for (const auto &port : configurer.ports()) {
		if (configurer.deviceClass(port) != QStringLiteral("videoDevice")) {
			continue;
		}

		Entry entry;
		DeviceState state("videoDevice");

		entry.w = static_cast<uint32_t>(
			ConfigurerHelper::configureInt(configurer, state, port, "width"));
		entry.h = static_cast<uint32_t>(
			ConfigurerHelper::configureInt(configurer, state, port, "height"));

		QString defaultDevFile, fmtStr, defaultStreamerScript;
		const auto devFile = configurer.attributeByPort(port, "device", &defaultDevFile);
		const auto fmt = configurer.attributeByPort(port, "format", &fmtStr);
		const auto streamerScript =
			configurer.attributeByPort(port, "mjpgStreamerScript", &defaultStreamerScript);

		// The ov7670 analog ports (named "video*") carry the I2C bus/address and
		// reset GPIO used for sensor initialization. Only those ports have them:
		// reading them from any other port (the USB webcam) would trip the
		// Configurer's malformed-attribute check. For non-video ports they stay 0.
		if (port.startsWith(QStringLiteral("video"))) {
			const auto optInt = [&configurer, &port](const QString &name) {
				try {
					bool ok = false;
					const int v = configurer.attributeByPort(port, name).toInt(&ok, 0);
					return ok ? v : 0;
				} catch (const trikKernel::MalformedConfigException &) {
					return 0;
				}
			};
			entry.i2cBus = optInt(QStringLiteral("i2cBus"));
			entry.i2cAddress = optInt(QStringLiteral("i2cAddress"));
			entry.gpioNumber = optInt(QStringLiteral("gpioNumber"));
		}

		entry.devFile = devFile;
		entry.mjpgStreamerScript = streamerScript;
		entry.ready = !state.isFailed() && !devFile.isEmpty() && !fmt.isEmpty();
		if (entry.ready) {
			entry.fmt = static_cast<uint32_t>(
				trikKernel::toV4l2Fourcc(trikDsp::pixelFormatFromString(fmt)));
		} else {
			QLOG_WARN() << "CameraManager: port" << port << "has an invalid config, marked not ready";
		}

		QLOG_INFO() << "CameraManager: registered port" << port << "->" << entry.devFile
			    << entry.w << 'x' << entry.h << "ready=" << entry.ready;

		mDevices.emplace(port, std::move(entry));
	}

	// Run the manager (and its V4L2 devices / QSocketNotifiers) on a dedicated
	// thread, so the slow sensor initialization and device I/O never block the
	// GUI thread. The static port state above was recorded before the move and
	// is never mutated afterwards except through the locked device entries.
	mThread.reset(new QThread);
	mThread->setObjectName(QStringLiteral("CameraManager"));
	moveToThread(mThread.data());
	mThread->start();

	// Kick off the (possibly slow) ov7670 initialization on the worker thread,
	// so the Brick constructor never blocks. It runs before any acquire() can,
	// because everything is serialized on the worker thread's event queue.
	runAsync([this]() { initSensors(); });
}

CameraManager::~CameraManager()
{
	// Tear the devices down on their own thread (blocking), then stop it.
	runInManagerThread([this]() {
		QWriteLocker lock(&mLock);
		tearDownLocked();
		mDevices.clear();
	});

	mThread->quit();
	mThread->wait();
}

void CameraManager::acquire(const QString &port)
{
	runAsync([this, port]() {
		bool ok = false;
		{
			QWriteLocker lock(&mLock);
			auto it = mDevices.find(port);
			if (it != mDevices.end() && it->second.ready) {
				// Open with the format recorded in the config.
				ok = openDeviceLocked(port, it->second);
			} else {
				QLOG_ERROR() << "CameraManager: port" << port << "is not available";
			}
		}
		emit acquired(port, ok);
	});
}

bool CameraManager::openDeviceLocked(const QString &port, Entry &entry)
{
	// Create the device only on the first acquisition, taking the format from
	// the port's static config. Later clients reuse it and just bump the
	// refcount, so the physical device is never opened twice and the format
	// of an already-open device stays intact.
	if (!entry.dev) {
		// The analog ov7670 cameras (ports named "video*") are initialized over
		// I2C by the kernel driver's reinit node; the USB webcam is not.
		const bool isVideoPort = port.startsWith(QStringLiteral("video"));

		// Initialize the ov7670 sensor (reinit via kernel driver, or a
		// full I2C register programming as a fallback) before opening the device.
		// It is done only once per process lifetime: the sensor keeps its
		// configuration across close/reopen, so a hot-plug re-open must not
		// re-run the slow init (the 1s exposure-stabilization sleep in the
		// fallback path) on an already-initialized sensor.
		if (isVideoPort && !entry.sensorInitialized) {
			entry.sensorInitialized = mHal.initVideoSensor(entry.devFile, entry.i2cBus,
								entry.i2cAddress, entry.gpioNumber);
		}

		std::unique_ptr<trikHal::VideoDeviceFileInterface> dev(
			mHal.createVideoDeviceFile(entry.devFile, entry.w, entry.h,
			                           entry.fmt, !isVideoPort));
		if (!dev->open() || !dev->startStreaming()) {
			QLOG_ERROR() << "CameraManager: failed to open" << entry.devFile;
			return false;
		}

		// Cache the actual negotiated format (may differ from the config) and
		// the bytes-per-line for the DSP descriptor.
		entry.format = trikKernel::fromV4l2Fourcc(dev->actualFourcc());
		entry.lineLength = dev->bytesPerLine();

		QLOG_DEBUG() << "CameraManager: port" << port << "opened, actualFourcc=0x"
		             << Qt::hex << dev->actualFourcc() << "lineLength" << entry.lineLength;

		// Fan out frames from the device to all subscribers of this port.
		connect(dev.get(), &trikHal::VideoDeviceFileInterface::frameReady, this,
		        [this, port](const uint8_t *data, size_t size) {
			onFrameReady(port, data, size);
		});

		entry.dev = std::move(dev);
		entry.streaming = true;
	}

	++entry.refCount;
	return true;
}

bool CameraManager::isOccupied(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return it != mDevices.end() && it->second.ready && it->second.refCount > 0;
}

void CameraManager::release(const QString &port)
{
	runAsync([this, port]() {
		std::unique_ptr<trikHal::VideoDeviceFileInterface> dev;
		{
			QWriteLocker lock(&mLock);
			auto it = mDevices.find(port);
			if (it == mDevices.end() || !it->second.ready || it->second.refCount <= 0)
				return;

			// Still used by other clients - nothing to tear down yet.
			if (--it->second.refCount > 0)
				return;

			// Last client gone: take ownership of the device out of the map,
			// drop all pending subscriptions, pull clients and the latched
			// frame, then release the lock before the slow teardown. The
			// device is destroyed when `dev` goes out of scope.
			dev = std::move(it->second.dev);
			it->second.streaming = false;
			mStreamingSubs.remove(port);
			mPullSubs.remove(port);
			mLatest.remove(port);
		}

		if (dev) {
			dev->stopStreaming();
			dev->close();
		}
	});
}

void CameraManager::stop()
{
	QLOG_INFO() << "CameraManager::stop: force-stopping all cameras";
	runInManagerThread([this]() {
		QWriteLocker lock(&mLock);
		tearDownLocked();
	});
	QLOG_INFO() << "CameraManager::stop: done";
}

void CameraManager::stop(const QString &port)
{
	QLOG_INFO() << "CameraManager::stop: force-stopping camera on port" << port;
	runInManagerThread([this, port]() {
		QWriteLocker lock(&mLock);
		auto it = mDevices.find(port);
		if (it != mDevices.end())
			tearDownPortLocked(port, it->second);
	});
	QLOG_INFO() << "CameraManager::stop: port" << port << "done";
}

void CameraManager::tearDownPortLocked(const QString &port, Entry &entry)
{
	if (entry.dev) {
		entry.dev->disconnect();
		entry.dev->stopStreaming();
		entry.dev->close();
		entry.dev.reset();
	}
	entry.refCount = 0;
	entry.streaming = false;
	mStreamingSubs.remove(port);
	mPullSubs.remove(port);
	mLatest.remove(port);
}

void CameraManager::tearDownLocked()
{
	for (auto &kv : mDevices) {
		tearDownPortLocked(kv.first, kv.second);
	}
}

void CameraManager::initSensors()
{
	// Runs in the worker thread (posted from the constructor). Only ov7670
	// ports carry an I2C bus (the USB webcam has none), so they are the
	// only ones that need sensor initialization. initVideoSensor() returns
	// false when no sensor is physically wired: such a port stays
	// uninitialized and is retried lazily on acquire(), which also covers a
	// later hot-plug.
	QWriteLocker lock(&mLock);
	for (auto &kv : mDevices) {
		auto &entry = kv.second;
		if (!entry.ready || entry.i2cBus <= 0 || entry.sensorInitialized) {
			continue;
		}
		entry.sensorInitialized = mHal.initVideoSensor(entry.devFile, entry.i2cBus,
		                                               entry.i2cAddress, entry.gpioNumber);
	}
}

void CameraManager::subscribe(const QString &port, QObject *receiver,
                              FrameCb callback)
{
	// Posted to the manager's thread so the subscription map stays owned by
	// the capture hot path (onFrameReady reads it lock-free). Cold path.
	runAsync([this, port, receiver, callback = std::move(callback)]() mutable {
		auto it = mDevices.find(port);
		if (it == mDevices.end() || !it->second.ready || !it->second.dev) {
			QLOG_WARN() << "CameraManager: cannot subscribe to" << port
			            << "(not acquired or not ready)";
			return;
		}

		mStreamingSubs[port] = StreamingSub{receiver, std::move(callback)};
		updateStreaming(port);
	});
}

void CameraManager::unsubscribe(const QString &port, QObject *receiver)
{
	runAsync([this, port, receiver]() {
		auto it = mStreamingSubs.find(port);
		if (it != mStreamingSubs.end() && it->receiver == receiver)
			mStreamingSubs.erase(it);
		updateStreaming(port);
	});
}

void CameraManager::subscribeLatest(const QString &port, QObject *receiver)
{
	runAsync([this, port, receiver]() {
		auto it = mDevices.find(port);
		if (it == mDevices.end() || !it->second.ready || !it->second.dev) {
			QLOG_WARN() << "CameraManager: cannot subscribe latest to" << port
			            << "(not acquired or not ready)";
			return;
		}

		mPullSubs[port].append(receiver);
		updateStreaming(port);

		// Auto-unsubscribe when the receiver dies, so a forgotten unsubscribe
		// cannot keep latching on forever. The handler runs in the manager's
		// thread (queued via the `this` context), so it may touch mPullSubs
		// directly. The connection dies together with the receiver.
		QObject::connect(receiver, &QObject::destroyed, this,
		                 [this, port]() { prunePullSubs(port); });
	});
}

void CameraManager::prunePullSubs(const QString &port)
{
	auto it = mPullSubs.find(port);
	if (it == mPullSubs.end())
		return;

	auto &pull = it.value();
	for (int i = pull.size() - 1; i >= 0; --i) {
		if (!pull[i])
			pull.removeAt(i);
	}
	if (pull.isEmpty())
		mPullSubs.erase(it);
	updateStreaming(port);
}

void CameraManager::unsubscribeLatest(const QString &port, QObject *receiver)
{
	runAsync([this, port, receiver]() {
		auto it = mPullSubs.find(port);
		if (it == mPullSubs.end())
			return;

		auto &pull = it.value();
		for (int i = pull.size() - 1; i >= 0; --i) {
			if (pull[i] == receiver)
				pull.removeAt(i);
		}
		if (pull.isEmpty())
			mPullSubs.erase(it);
		updateStreaming(port);
	});
}

void CameraManager::updateStreaming(const QString &port)
{
	// Runs in the manager's thread (called from subscribe*/unsubscribe*).
	// The device must stream iff at least one push or pull subscriber wants
	// frames. This parks the camera when the last video-sensor push client
	// unsubscribes (StopStream) and transparently resumes it when a pull client
	// (getPhoto) subscribes - no client races over a shared flag.
	QWriteLocker lock(&mLock);
	auto it = mDevices.find(port);
	if (it == mDevices.end() || !it->second.ready || !it->second.dev)
		return;

	const auto streamingIt = mStreamingSubs.constFind(port);
	const bool hasStreaming = streamingIt != mStreamingSubs.constEnd() && streamingIt->receiver;
	const auto pullIt = mPullSubs.constFind(port);
	const bool hasPull = pullIt != mPullSubs.constEnd() && !pullIt->isEmpty();

	if (hasStreaming || hasPull) {
		if (!it->second.streaming) {
			it->second.dev->startStreaming();
			it->second.streaming = true;
		}
		// A continuous (push) consumer wants a stable, locked exposure. This is
		// idempotent on the device, so it is requested on every subscription
		// change, not only when streaming was just started - otherwise a push
		// subscriber arriving after the initial open would never get the lock.
		if (hasStreaming)
			it->second.dev->fixExposure();
	} else if (it->second.streaming) {
		it->second.dev->stopStreaming();
		it->second.streaming = false;
	}
}

QSharedPointer<QByteArray> CameraManager::grabLatestFrame(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mLatest.constFind(port);
	return (it != mLatest.constEnd()) ? it.value() : QSharedPointer<QByteArray>();
}

void CameraManager::onFrameReady(const QString &port, const uint8_t *data, size_t size)
{
	// Capture hot path. mStreamingSubs / mPullSubs are mutated only in the
	// manager's thread (subscribe*/unsubscribe* are marshalled here), so they
	// are read lock-free. Only the latched-frame publish takes a lock, to stay
	// visible to grabLatestFrame() called from other threads.

	// Streaming (push) subscriber: at most one, owns the buffer release. Copy
	// the callback out so a re-entrant subscribe() from inside it cannot
	// invalidate the map entry mid-call.
	FrameCb streamingCb;
	const auto sit = mStreamingSubs.constFind(port);
	const bool hasStreaming = sit != mStreamingSubs.constEnd() && sit->receiver;
	if (hasStreaming)
		streamingCb = sit->callback;

	// Pull clients: latching is active while any is registered. Dead receivers
	// are removed by their own destroyed() handler (subscribeLatest), so this
	// is a plain O(1) lookup with no traversal.
	const auto pit = mPullSubs.constFind(port);
	const bool latch = pit != mPullSubs.constEnd() && !pit->isEmpty();

	if (hasStreaming)
		streamingCb(data, size);

	if (latch) {
		auto frame = QSharedPointer<QByteArray>::create(
			reinterpret_cast<const char *>(data), static_cast<int>(size));
		{
			QWriteLocker lock(&mLock);
			mLatest[port] = std::move(frame);
		}
		emit latestFrameReady(port);
	}

	// Release policy: the streaming subscriber owns releaseFrame() (backpressure);
	// with none, return the buffer now so pull clients keep receiving frames.
	if (!hasStreaming)
		releaseFrame(port);
}

void CameraManager::releaseFrame(const QString &port)
{
	// The streaming consumer owns the buffer release (backpressure). Posting it
	// here keeps the caller's thread (the GUI thread, in onResult) responsive:
	// the QBUF happens in the worker thread a moment later, still gated by the
	// disabled QSocketNotifier, so no buffer overruns.
	runAsync([this, port]() {
		QReadLocker lock(&mLock);
		auto it = mDevices.find(port);
		if (it != mDevices.end() && it->second.dev)
			it->second.dev->release();
	});
}

uint32_t CameraManager::width(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return (it != mDevices.end() && it->second.ready) ? it->second.w : 0;
}

uint32_t CameraManager::height(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return (it != mDevices.end() && it->second.ready) ? it->second.h : 0;
}

uint32_t CameraManager::fourcc(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return (it != mDevices.end() && it->second.ready) ? it->second.fmt : 0;
}

trikKernel::PixelFormat CameraManager::format(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return (it != mDevices.end() && it->second.ready) ? it->second.format : trikKernel::PixelFormat::Unknown;
}

uint32_t CameraManager::lineLength(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return (it != mDevices.end() && it->second.ready) ? it->second.lineLength : 0;
}

bool CameraManager::isReady(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return it != mDevices.end() && it->second.ready;
}

QString CameraManager::deviceFile(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return (it != mDevices.end() && it->second.ready) ? it->second.devFile : QString();
}

QString CameraManager::streamerScript(const QString &port) const
{
	QReadLocker lock(&mLock);
	auto it = mDevices.find(port);
	return it != mDevices.end() ? it->second.mjpgStreamerScript : QString();
}

} // namespace trikControl

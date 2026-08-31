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

#include <QtCore/QVector>

#include <trikHal/VideoDeviceFileInterface.h>
#include <trikHal/hardwareAbstractionInterface.h>
#include <trikHal/physicalMemoryMapper.h>
#include <trikKernel/configurer.h>
#include <trikKernel/videoUtils.h>
#include <trikDsp/dspTypes.h>
#include <QsLog.h>

#include "configurerHelper.h"
#include "deviceState.h"

namespace trikControl {

namespace {

/// Fixed physical address of the capture region (`in_buff`). The DSP linker
/// places it right after `.resource_table` in DDR, so it is a build-time
/// constant (see trikDsp/trik-media-sensors/dsp/bin/*/obj/server_dsp.xe674.map,
/// `in_buff @ 0xc4000100`). Mapping it directly via /dev/mem lets the VPIF DMA
/// engine capture into that memory without waiting for the DSP's INIT response.
constexpr uintptr_t INPUT_PHYS = 0xc4000100;

/// Capture region layout (mirrors trik/buffer.h).
///
/// One buffer holds one 320x240 YUV422 frame: 2 bytes per pixel, so
/// 320 * 240 * 2 = 153600 bytes. Three buffers per region give triple
/// buffering, so one region is 3 * 153600 = 460800 bytes (0x70800); the three
/// regions together are 9 * 153600 = 1382400 bytes (0x151800), matching the
/// `in_buff` symbol in the DSP map file.
constexpr uint32_t INPUT_REGIONS = 3;
constexpr uint32_t INPUT_BUFFERS_PER_REGION = 3;
constexpr uint32_t INPUT_TOTAL = INPUT_REGIONS * INPUT_BUFFERS_PER_REGION;
constexpr size_t INPUT_BUFFER_LEN = 320 * 240 * 2;

} // namespace

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

	// Assign each video port a capture region up front (one per port, so every
	// camera always streams into its own DSP memory). The assignment is static:
	// CameraManager and VideoSensorManager both read it via inputRegion().
	auto nextRegion = 0u;
	for (auto &kv : mDevices) {
		mPortRegions[kv.first] = nextRegion;
		nextRegion = (nextRegion + 1) % INPUT_REGIONS;
	}

	// Map the capture region up front (before sensors start, and before the
	// DSP's INIT response): the manager learns the per-port capture ranges here
	// so acquire() can set up USERPTR zero-copy streaming at any time.
	if (mapInputRegion()) {
		QLOG_INFO() << "CameraManager: capture region mapped, zero-copy streaming available";
	} else {
		QLOG_WARN() << "CameraManager: capture region unavailable, USERPTR disabled (MMAP fallback)";
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
	// mInputMap munmaps itself (RAII).
}

void CameraManager::acquire(const QString &port)
{
	runAsync([this, port]() {
		bool ok = false;
		{
			QWriteLocker lock(&mLock);
			auto it = mDevices.find(port);
			if (it != mDevices.end() && it->second.ready) {
				ok = openDeviceLocked(port, it->second);
				// A client that grabs the camera wants frames: resume a parked
				// camera (startStreaming), and clear any pending park request.
				if (ok && it->second.dev && !it->second.streaming) {
					if (it->second.dev->startStreaming()) {
						it->second.streaming = true;
					} else {
						// Streaming failed to start: undo the acquisition so the
						// client does not hold a non-streaming camera forever.
						ok = false;
						--it->second.refCount;
					}
				}
				it->second.stopRequested = false;
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

		// Zero-copy: capture straight into the port's capture region (USERPTR),
		// which was assigned and mapped in the constructor. Falls back to MMAP
		// when the region is unavailable (no /dev/mem).
		if (mInputMap) {
			const auto region = mPortRegions.value(port, 0);
			const auto base = region * INPUT_BUFFERS_PER_REGION;
			QVector<void *> buffers;
			buffers.reserve(static_cast<int>(INPUT_BUFFERS_PER_REGION));
			for (uint32_t i = 0; i < INPUT_BUFFERS_PER_REGION; ++i) {
				buffers.append(mInputMap.data() + (base + i) * INPUT_BUFFER_LEN);
			}
			dev->setUserPtrBuffers(buffers, INPUT_BUFFER_LEN);
			QLOG_INFO() << "CameraManager: port" << port << "capturing into region" << region;
		}

		if (!dev->open()) {
			QLOG_ERROR() << "CameraManager: failed to open" << entry.devFile;
			return false;
		}

		// Cache the actual negotiated format (may differ from the config) and
		// the bytes-per-line for the DSP descriptor.
		entry.format = trikKernel::fromV4l2Fourcc(dev->actualFourcc());
		entry.lineLength = dev->bytesPerLine();

		QLOG_DEBUG() << "CameraManager: port" << port << "opened, actualFourcc=0x"
		             << Qt::hex << dev->actualFourcc() << "lineLength" << entry.lineLength;

		// Relay frames from the device to all consumers of this port.
		connect(dev.get(), &trikHal::VideoDeviceFileInterface::frameReady, this,
		        [this, port](uint32_t bufferIdx, const uint8_t *data, size_t size) {
			onDeviceFrame(port, bufferIdx, data, size);
		});

		entry.dev = std::move(dev);
	}

	++entry.refCount;
	return true;
}

void CameraManager::onDeviceFrame(const QString &port, uint32_t bufferIdx,
                                  const uint8_t *data, size_t size)
{
	QWriteLocker lock(&mLock);
	auto it = mDevices.find(port);
	if (it == mDevices.end() || !it->second.dev) {
		return;
	}

	auto &refCounts = it->second.frameRefCount;

	// Auto-return every delivered-but-unclaimed frame of this port: nobody
	// grabbed it (refcount 0), so let the driver reuse/overwrite the buffer.
	for (auto refIt = refCounts.begin(); refIt != refCounts.end();) {
		if (refIt.value() == 0) {
			it->second.dev->release(refIt.key());
			refIt = refCounts.erase(refIt);
		} else {
			++refIt;
		}
	}

	// Deliver the new frame, initially unclaimed (refcount 0).
	refCounts[bufferIdx] = 0;
	emit frameReady(port, bufferIdx, data, size);
}

void CameraManager::stopStreaming(const QString &port)
{
	runAsync([this, port]() {
		QWriteLocker lock(&mLock);
		auto it = mDevices.find(port);
		if (it == mDevices.end() || !it->second.dev) {
			return;
		}
		// Remember the park request. If the camera is not needed by anyone else
		// (the requester is its only client), park it right away; otherwise the
		// park happens in release() when the last client lets go.
		it->second.stopRequested = true;
		if (it->second.refCount <= 1 && it->second.streaming) {
			it->second.dev->stopStreaming();
			it->second.streaming = false;
			// STREAMOFF returns every buffer to the driver; forget our refcounts.
			it->second.frameRefCount.clear();
		}
	});
}

void CameraManager::release(const QString &port)
{
	runAsync([this, port]() {
		std::unique_ptr<trikHal::VideoDeviceFileInterface> dev;
		{
			QWriteLocker lock(&mLock);
			auto it = mDevices.find(port);
			if (it == mDevices.end() || !it->second.ready || it->second.refCount <= 0) {
				return;
			}

			// Still used by other clients - nothing to tear down yet.
			if (--it->second.refCount > 0) {
				return;
			}

			// Last client gone. If a stop was requested, only park the camera
			// (streamoff) and keep it open so a later acquire() starts it again.
			// Otherwise tear the device down for good.
			if (it->second.stopRequested) {
				if (it->second.streaming) {
					it->second.dev->stopStreaming();
					it->second.streaming = false;
				}
				it->second.frameRefCount.clear();
				return;
			}

			// Take ownership of the device out of the map, then release the
			// lock before the slow teardown. The device is destroyed when `dev`
			// goes out of scope.
			dev = std::move(it->second.dev);
			it->second.streaming = false;
			it->second.frameRefCount.clear();
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
	Q_UNUSED(port);
	if (entry.dev) {
		entry.dev->disconnect();
		entry.dev->stopStreaming();
		entry.dev->close();
		entry.dev.reset();
	}
	entry.refCount = 0;
	entry.stopRequested = false;
	entry.streaming = false;
	entry.frameRefCount.clear();
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

void CameraManager::retainFrame(const QString &port, uint32_t bufferIdx)
{
	runAsync([this, port, bufferIdx]() {
		QWriteLocker lock(&mLock);
		auto it = mDevices.find(port);
		if (it == mDevices.end() || !it->second.dev) {
			return;
		}

		auto &refCounts = it->second.frameRefCount;
		const auto refIt = refCounts.find(bufferIdx);
		// Only a currently-delivered frame (one that has an entry, initially 0)
		// can be claimed. If the entry is gone the frame was already
		// auto-returned to the driver (a newer frame superseded it before we
		// got here) - claiming it now would later produce a double QBUF.
		if (refIt != refCounts.end()) {
			++refIt.value();
		}
	});
}

void CameraManager::releaseFrame(const QString &port, uint32_t bufferIdx)
{
	runAsync([this, port, bufferIdx]() {
		QWriteLocker lock(&mLock);
		auto it = mDevices.find(port);
		if (it == mDevices.end() || !it->second.dev) {
			return;
		}

		auto &refCounts = it->second.frameRefCount;
		const auto refIt = refCounts.find(bufferIdx);
		// The buffer was never claimed, was already recycled, or was never
		// retained at all (refcount 0) - nothing to hand back. The manager
		// auto-returns unclaimed frames when the next one arrives. This also
		// guards against a stray double release.
		if (refIt == refCounts.end() || refIt.value() == 0) {
			return;
		}

		if (--refIt.value() > 0) {
			return;
		}

		refCounts.erase(refIt);
		it->second.dev->release(bufferIdx);
	});
}

bool CameraManager::mapInputRegion()
{
	const auto regionLen = INPUT_TOTAL * INPUT_BUFFER_LEN;
	mInputMap = trikHal::mapPhysicalMemory(INPUT_PHYS, regionLen);
	if (!mInputMap) {
		QLOG_WARN() << "CameraManager: failed to map capture region at 0x" << Qt::hex << INPUT_PHYS;
		return false;
	}

	QLOG_INFO() << "CameraManager: mapped capture region at"
	            << static_cast<void *>(mInputMap.data()) << "size" << regionLen;
	return true;
}

uint32_t CameraManager::inputBuffersPerRegion() const
{
	return INPUT_BUFFERS_PER_REGION;
}

size_t CameraManager::inputBufferLen() const
{
	return INPUT_BUFFER_LEN;
}

uint32_t CameraManager::inputRegion(const QString &port) const
{
	QReadLocker lock(&mLock);
	const auto it = mPortRegions.find(port);
	return it != mPortRegions.end() ? it.value() : 0;
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

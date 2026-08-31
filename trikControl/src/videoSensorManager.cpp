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

#include "videoSensorManager.h"

#include <memory>

#include <trikKernel/configurer.h>
#include <QsLog.h>

#include <trikHal/VideoDeviceFileInterface.h>
#include <trikHal/hardwareAbstractionInterface.h>
#include <trikDsp/dspTypes.h>

#include "configurerHelper.h"
#include "dspSensorBase.h"

namespace trikControl {

namespace { constexpr uint16_t DSP_RPROC_ID = 1; }

VideoSensorManager::VideoSensorManager(const trikKernel::Configurer &configurer,
				       const trikHal::HardwareAbstractionInterface &hardwareAbstraction,
				       CameraManager *cameraManager)
	: mConfigurer(configurer)
	, mHardwareAbstractionInterface(hardwareAbstraction)
	, mState("VideoSensorManager")
	, mCameraManager(cameraManager)
{
	mDspThread.reset(new QThread);
	mDspThread->setObjectName(QStringLiteral("DspServer"));

	mDspServer.reset(new trikDsp::DspServer(DSP_RPROC_ID));
	mDspServer->setFbOutput(mHardwareAbstractionInterface.createFbOutput());

	connect(mDspServer.data(), &trikDsp::DspServer::errorOccurred, this, [this](const QString &message) {
		QLOG_ERROR() << "The VideoSensorManager constructor terminated with an error:" << message;
		mState.fail();
	});
	connect(mDspServer.data(), &trikDsp::DspServer::successfullyInited, this, [this]() { mState.ready(); });
	connect(mDspServer.data(), &trikDsp::DspServer::resultReady, this, &VideoSensorManager::onResult);

	// The camera manager lives on its own thread, so its acquire() is async and
	// reports completion via this queued signal.
	connect(mCameraManager, &CameraManager::acquired, this, &VideoSensorManager::onAcquired);

	// Frames arrive from the CameraManager's worker thread. The camera streams
	// at its own rate; the DSP is a "latest wins" consumer and must not be
	// forced to process every frame (it may not keep up). For the DSP-active
	// port we keep only the newest ready frame and hand it to the DSP when it is
	// idle; stale frames are released straight away. Frames of other ports are
	// simply ignored - the manager auto-returns unclaimed buffers, and a parked
	// camera does not stream at all. The data pointer is not used - the DSP
	// reads the frame directly from its input buffer by index.
	connect(mCameraManager, &CameraManager::frameReady, this,
	        [this](const QString &port, uint32_t bufferIdx, const uint8_t *data, size_t size) {
		Q_UNUSED(data);
		Q_UNUSED(size);
		if (!mActivePorts.contains(port) || port != mActiveDspPort) {
			return;
		}

		// Active DSP channel, latest wins: supersede the previous pending frame
		// (release it - it was never handed to the DSP), keep the newest.
		const auto it = mPendingFrames.find(port);
		if (it != mPendingFrames.end()) {
			mCameraManager->releaseFrame(port, it.value());
		}
		mCameraManager->retainFrame(port, bufferIdx);
		mPendingFrames[port] = bufferIdx;
		kickDsp(port);
	});

	// init() is intentionally synchronous here: it starts the IPC stack and
	// mmaps the DSP output buffer, which every subsequent activate()/step()
	// depends on. The block runs on the GUI thread, but init() spins a local
	// QEventLoop, so the UI stays responsive during the (worst-case 15s) wait.
	mDspServer->init();
	if (!mState.isReady()) return;

	mDspServer->moveToThread(mDspThread.data());
	mDspThread->start();
}

void VideoSensorManager::destroyDsp() { mDspThread->quit(); mDspThread->wait(); mDspServer.reset(); }

VideoSensorManager::~VideoSensorManager()
{
	destroyDsp();
	mPendingActivations.clear();
	for (const auto &port : mActivePorts) mCameraManager->release(port);
	mActivePorts.clear();

	qDeleteAll(mLineSensors); mLineSensors.clear();
	qDeleteAll(mColorSensors); mColorSensors.clear();
	qDeleteAll(mObjectSensors); mObjectSensors.clear();
	qDeleteAll(mJpegEncoders); mJpegEncoders.clear();
}

bool VideoSensorManager::checkManagerState(const QString &message) const
{
	if (!mState.isReady()) {
		QLOG_ERROR() << "VideoSensorManager: attempt to " << message << " on uninitialized";
		return false;
	}
	return true;
}

void VideoSensorManager::createSensor(const QString &port, const QString &deviceClass)
{
	// A port can host at most one sensor of a given class. Re-entering create()
	// with the same (port, class) is a no-op: the sensor is already there and
	// its signal wiring is intact, so there is nothing to do.
	if (deviceClass == QStringLiteral("lineSensor")) {
		if (mLineSensors.contains(port)) {
			// QLOG_INFO() << "VideoSensorManager: lineSensor on port" << port << "already created";
			return;
		}
		auto *s = new LineSensor(port, mConfigurer);
		connect(s, &LineSensor::activateRequested, this,
		        [this, port](const trikDsp::InArgs &args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Line, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &LineSensor::stopRequested, this,
		        [this, port](int flags) { handleStopRequested(port, flags); }, Qt::QueuedConnection);
		mLineSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("objectSensor")) {
		if (mObjectSensors.contains(port)) {
			// QLOG_INFO() << "VideoSensorManager: objectSensor on port" << port << "already created";
			return;
		}
		auto *s = new ObjectSensor(port, mConfigurer);
		connect(s, &ObjectSensor::activateRequested, this,
		        [this, port](const trikDsp::InArgs &args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Object, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ObjectSensor::stopRequested, this,
		        [this, port](int flags) { handleStopRequested(port, flags); }, Qt::QueuedConnection);
		mObjectSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("colorSensor")) {
		if (mColorSensors.contains(port)) {
			// QLOG_INFO() << "VideoSensorManager: colorSensor on port" << port << "already created";
			return;
		}
		auto *s = new ColorSensor(port, mConfigurer);
		connect(s, &ColorSensor::activateRequested, this,
		        [this, port](const trikDsp::InArgs &args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Mxn, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ColorSensor::stopRequested, this,
		        [this, port](int flags) { handleStopRequested(port, flags); }, Qt::QueuedConnection);
		mColorSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("jpegEncoderSensor")) {
		if (mJpegEncoders.contains(port)) {
			// QLOG_INFO() << "VideoSensorManager: jpegEncoderSensor on port" << port << "already created";
			return;
		}
		auto *s = new JpegEncoderSensor(port, mConfigurer, mHardwareAbstractionInterface);
		connect(s, &JpegEncoderSensor::activateRequested, this,
		        [this, port](const trikDsp::InArgs &args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Jpeg, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &JpegEncoderSensor::stopRequested, this,
		        [this, port](int flags) { handleStopRequested(port, flags); }, Qt::QueuedConnection);
		mJpegEncoders.insert(port, s);
	}
}

void VideoSensorManager::activateForPort(const QString &port, trikDsp::Algorithm algo,
                                         const trikDsp::InArgs &args, bool videoOut, bool canOpen)
{
	// An acquire is already in flight: the latest request wins, whether it is a
	// re-init or a detect() re-activation that arrived before the camera opened.
	if (mPendingActivations.contains(port)) {
		mPendingActivations[port] = {algo, args, videoOut};
		return;
	}

	if (!mActivePorts.contains(port)) {
		// The camera is not held. Start the (async) acquisition and defer the
		// DSP activation to onAcquired(). CameraManager owns the capture region
		// (assigned and mapped up front, before sensors start) and captures the
		// port's frames straight into its own region via USERPTR, so several
		// cameras stream simultaneously.
		if (canOpen) {
			mPendingActivations[port] = {algo, args, videoOut};
			mCameraManager->acquire(port);
		}
		return;
	}

	// The camera is already held. (Re)activate the DSP directly.
	activateDsp(port, algo, args, videoOut);
}

void VideoSensorManager::onAcquired(const QString &port, bool ok)
{
	auto it = mPendingActivations.find(port);
	if (it == mPendingActivations.end())
		return;

	const auto activation = it.value();
	mPendingActivations.erase(it);

	if (!ok) {
		QLOG_ERROR() << "VideoSensorManager: failed to acquire camera for port" << port;
		return;
	}

	mActivePorts.insert(port);
	QLOG_INFO() << "VideoSensorManager: camera acquired for port" << port;

	activateDsp(port, activation.algo, activation.args, activation.videoOut);
}

void VideoSensorManager::activateDsp(const QString &port, trikDsp::Algorithm algo,
                                     const trikDsp::InArgs &args, bool videoOut)
{
	// The DSP CV algorithms must know the actual pixel format (NV16 vs YUYV) and
	// the actual bytes-per-line to decode the frame correctly. They are taken
	// from the CameraManager (cached at acquire), not from the raw device. The
	// channel also records the port's DSP input region base so the DSP maps the
	// per-port V4L2 buffer index to the correct input buffer.
	//
	// Switching the DSP to another port: release any frame still pending for the
	// old port (its camera keeps streaming; VSM stops consuming its frames).
	if (!mActiveDspPort.isEmpty() && mActiveDspPort != port) {
		clearPendingFrame(mActiveDspPort);
	}

	const auto base = mCameraManager->inputRegion(port) * mCameraManager->inputBuffersPerRegion();
	mDspServer->activate({port, algo, args, videoOut,
	                      mCameraManager->width(port), mCameraManager->height(port),
	                      mCameraManager->format(port), mCameraManager->lineLength(port),
	                      base});
	mActiveDspPort = port;
}

void VideoSensorManager::kickDsp(const QString &port)
{
	// Try to hand the freshest pending frame to the DSP, but only when its
	// processing slot is free. Called from frameReady (slot free -> hand over
	// right away) and from onResult (slot just freed -> pick up any frame that
	// arrived while the DSP was busy). Invariant: a pending frame never waits
	// longer than one onResult, so the DSP is always fed the newest frame and
	// never accumulates a backlog.
	if (mDspBusyPorts.contains(port)) {
		return;
	}
	const auto it = mPendingFrames.find(port);
	if (it == mPendingFrames.end()) {
		return;
	}

	const auto bufferIdx = it.value();
	mPendingFrames.erase(it);
	mDspBusyPorts.insert(port);

	QMetaObject::invokeMethod(mDspServer.data(), [this, port, bufferIdx]() {
		mDspServer->processFrameData(port, bufferIdx);
	}, Qt::QueuedConnection);
}

void VideoSensorManager::clearPendingFrame(const QString &port)
{
	const auto it = mPendingFrames.find(port);
	if (it == mPendingFrames.end()) {
		return;
	}
	mCameraManager->releaseFrame(port, it.value());
	mPendingFrames.erase(it);
}

void VideoSensorManager::handleStopRequested(const QString &port, int flags)
{
	// The DSP is single-channel: deactivate it only when the port being stopped
	// actually owns the active channel. Otherwise stopping a background sensor
	// would silently kill the channel of the sensor currently streaming on
	// another port (and leave mActiveDspPort pointing at a dead channel).
	if (mActiveDspPort == port) {
		mDspServer->deactivate();
		clearPendingFrame(port);
		mActiveDspPort.clear();
	}

	// A stop cancels any acquire still in flight for this port; the released
	// camera is closed by the manager's queued release() even if the acquire
	// later completes (refcount is preserved by the manager's event order).
	mPendingActivations.remove(port);

	if (flags & StopStream) {
		// Park the camera: keep it open but stop the stream. release() with the
		// park flag leaves the device open for a quick re-acquire().
		mCameraManager->stopStreaming(port);
		mCameraManager->release(port);
		mActivePorts.remove(port);
	} else if (flags & StopAll) {
		mCameraManager->release(port);
		mActivePorts.remove(port);
	}
	// StopNone: only the DSP is deactivated; the camera keeps streaming.

	// Repaint the display only when the camera was actually stopped or streamed
	// off, i.e. when the framebuffer may hold a stale frame. A pure algorithm
	// switch (StopNone) keeps the camera running and just replaces the DSP
	// channel, so there is nothing to clear.
	if (flags & (StopStream | StopAll)) {
		Q_EMIT sensorStopped();
	}
}

void VideoSensorManager::create(const QString &port, const QString &deviceClass)
{
	if (!checkManagerState("create")) return;
	createSensor(port, deviceClass);
}

void VideoSensorManager::stop()
{
	QLOG_INFO() << "VideoSensorManager::stop: called, activePorts=" << mActivePorts.size()
	            << "pendingActivations=" << mPendingActivations.size();
	if (!checkManagerState("stop")) return;

	// The DSP is single-channel: if the detached encoder is the active channel
	// it must keep streaming, so skip the deactivation.
	if (!mActiveDspPort.isEmpty() && mDetachedPorts.contains(mActiveDspPort)) {
		QLOG_INFO() << "VideoSensorManager::stop: keeping detached channel" << mActiveDspPort << "active";
	} else {
		mDspServer->deactivate();
		clearPendingFrame(mActiveDspPort);
		mActiveDspPort.clear();
		QLOG_INFO() << "VideoSensorManager::stop: DSP deactivated";
	}
	mPendingActivations.clear();

	// Stop and release every held camera except detached ports.
	for (auto it = mActivePorts.begin(); it != mActivePorts.end();) {
		if (mDetachedPorts.contains(*it)) {
			++it;
			continue;
		}
		clearPendingFrame(*it);
		mCameraManager->release(*it);
		it = mActivePorts.erase(it);
	}
	QLOG_INFO() << "VideoSensorManager::stop: active ports released";
}

void VideoSensorManager::releasePort(const QString &port)
{
	// The DSP is single-channel: deactivate it only when the port being
	// released actually owns the active channel. Releasing an unrelated port
	// (e.g. usb-camera, which never uses the DSP) must not kill the encoder
	// currently streaming another video port.
	if (mActiveDspPort == port) {
		mDspServer->deactivate();
		clearPendingFrame(port);
		mActiveDspPort.clear();
	}
	mPendingActivations.remove(port);
	mDetachedPorts.remove(port);

	// Fully release our camera hold: decrement the refcount, so the
	// CameraManager stays balanced. The caller (Brick::startVideoTranslation)
	// then either hands the device to mjpg-streamer (USB) or re-acquires it for
	// the DSP JPEG encoder (video ports). A plain "forget the port" would leak
	// the sensor's acquire() and leave the camera open forever.
	if (mActivePorts.remove(port)) {
		mCameraManager->release(port);
	}
}

void VideoSensorManager::setPortDetached(const QString &port, bool detached)
{
	if (detached) {
		mDetachedPorts.insert(port);
	} else {
		mDetachedPorts.remove(port);
	}
}

void VideoSensorManager::stopTranslation(const QString &port, bool keepCamera)
{
	mDetachedPorts.remove(port);

	auto it = mJpegEncoders.find(port);
	if (it == mJpegEncoders.end()) {
		return;
	}

	it.value()->stop(keepCamera ? StopStream : StopAll);
}

void VideoSensorManager::clear()
{
	const auto eraseNonDetached = [this](auto &map) {
		for (auto it = map.begin(); it != map.end();) {
			if (mDetachedPorts.contains(it.key())) {
				++it;
			} else {
				delete it.value();
				it = map.erase(it);
			}
		}
	};
	eraseNonDetached(mLineSensors);
	eraseNonDetached(mColorSensors);
	eraseNonDetached(mObjectSensors);
	eraseNonDetached(mJpegEncoders);
}

bool VideoSensorManager::isVideoSensor(const QString &deviceClass)
{
	return deviceClass == QStringLiteral("lineSensor")
	    || deviceClass == QStringLiteral("objectSensor")
	    || deviceClass == QStringLiteral("colorSensor");
}

LineSensorInterface *VideoSensorManager::lineSensor(const QString &port) {
	auto it = mLineSensors.find(port); return it == mLineSensors.end() ? nullptr : *it;
}
ColorSensorInterface *VideoSensorManager::colorSensor(const QString &port) {
	auto it = mColorSensors.find(port); return it == mColorSensors.end() ? nullptr : *it;
}
ObjectSensorInterface *VideoSensorManager::objectSensor(const QString &port) {
	auto it = mObjectSensors.find(port); return it == mObjectSensors.end() ? nullptr : *it;
}

JpegEncoderSensor *VideoSensorManager::jpegEncoderSensor(const QString &port)
{
	if (!checkManagerState("jpegEncoderSensor")) return nullptr;
	createSensor(port, QStringLiteral("jpegEncoderSensor"));
	auto it = mJpegEncoders.find(port);
	return it == mJpegEncoders.end() ? nullptr : *it;
}

void VideoSensorManager::onResult(const QString &sourceId, trikDsp::Algorithm algorithm,
                                  const trikDsp::OutArgs &result, uint32_t bufferIdx)
{
	// The DSP finished (or dropped) the frame: hand the buffer back and free the
	// processing slot. This is what re-feeds the DSP - kickDsp() picks up the
	// freshest frame that arrived while it was busy, so stale frames never queue
	// up (latest wins).
	mCameraManager->releaseFrame(sourceId, bufferIdx);
	mDspBusyPorts.remove(sourceId);
	kickDsp(sourceId);

	switch (algorithm) {
	case trikDsp::Algorithm::Line: {
		auto it = mLineSensors.find(sourceId);
		if (it != mLineSensors.end()) it.value()->onResult(result);
		break;
	}
	case trikDsp::Algorithm::Object: {
		auto it = mObjectSensors.find(sourceId);
		if (it != mObjectSensors.end()) it.value()->onResult(result);
		break;
	}
	case trikDsp::Algorithm::Mxn: {
		auto it = mColorSensors.find(sourceId);
		if (it != mColorSensors.end()) it.value()->onResult(result);
		break;
	}
	case trikDsp::Algorithm::Jpeg: {
		auto it = mJpegEncoders.find(sourceId);
		if (it != mJpegEncoders.end()) it.value()->onResult(result);
		break;
	}
	default: break;
	}
}

}

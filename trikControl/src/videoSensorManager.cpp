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
	connect(mDspServer.data(), &trikDsp::DspServer::videoDisplayStarted, this, &VideoSensorManager::videoDisplayStarted);
	connect(mDspServer.data(), &trikDsp::DspServer::videoDisplayFinished, this, &VideoSensorManager::videoDisplayFinished);

	// The camera manager lives on its own thread, so its acquire() is async and
	// reports completion via this queued signal.
	connect(mCameraManager, &CameraManager::acquired, this, &VideoSensorManager::onAcquired);

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
			QLOG_INFO() << "VideoSensorManager: lineSensor on port" << port << "already created";
			return;
		}
		auto *s = new LineSensor(port, mConfigurer);
		connect(s, &LineSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Line, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &LineSensor::stopRequested, this,
		        [this, port](bool deinit) { handleStopRequested(port, deinit); }, Qt::QueuedConnection);
		mLineSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("objectSensor")) {
		if (mObjectSensors.contains(port)) {
			QLOG_INFO() << "VideoSensorManager: objectSensor on port" << port << "already created";
			return;
		}
		auto *s = new ObjectSensor(port, mConfigurer);
		connect(s, &ObjectSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Object, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ObjectSensor::stopRequested, this,
		        [this, port](bool deinit) { handleStopRequested(port, deinit); }, Qt::QueuedConnection);
		mObjectSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("colorSensor")) {
		if (mColorSensors.contains(port)) {
			QLOG_INFO() << "VideoSensorManager: colorSensor on port" << port << "already created";
			return;
		}
		auto *s = new ColorSensor(port, mConfigurer);
		connect(s, &ColorSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Mxn, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ColorSensor::stopRequested, this,
		        [this, port](bool deinit) { handleStopRequested(port, deinit); }, Qt::QueuedConnection);
		mColorSensors.insert(port, s);
	}
}

void VideoSensorManager::activateForPort(const QString &port, trikDsp::Algorithm algo,
                                         trikDsp::InArgs args, bool videoOut, bool canOpen)
{
	// An acquire is already in flight: the latest request wins, whether it is a
	// re-init or a detect() re-activation that arrived before the camera opened.
	if (mPendingActivations.contains(port)) {
		mPendingActivations[port] = {algo, args, videoOut};
		return;
	}

	if (!mActivePorts.contains(port)) {
		// The camera is not held. Start the (async) acquisition and defer the
		// DSP activation to onAcquired(); without the right to open the camera
		// there is nothing to do.
		if (canOpen) {
			mPendingActivations[port] = {algo, args, videoOut};
			mCameraManager->acquire(port);
		}
		return;
	}

	activateDsp(port, algo, args, videoOut);
}

void VideoSensorManager::onAcquired(const QString &port, bool ok)
{
	auto it = mPendingActivations.find(port);
	if (it == mPendingActivations.end())
		return;

	const PendingActivation activation = it.value();
	mPendingActivations.erase(it);

	if (!ok) {
		QLOG_ERROR() << "VideoSensorManager: failed to acquire camera for port" << port;
		return;
	}

	// The camera is open and streaming. Subscribe to frames and activate the DSP.
	mCameraManager->subscribe(port, this, [this, port](const uint8_t *data, size_t size) {
		mCameraFrameCount++;
		if (mCameraFpsTimer.hasExpired(1000) || !mCameraFpsTimer.isValid()) {
			QLOG_INFO() << "CAM FPS:" << mCameraFrameCount;
			mCameraFrameCount = 0; mCameraFpsTimer.restart();
		}
		// Lightweight callback: copy the frame into the DSP shared buffer,
		// then queue processing on the DspServer thread. The data pointer is
		// zero-copy and valid only within this callback.
		mDspServer->copyFrame(data, size);
		QMetaObject::invokeMethod(mDspServer.data(), [this, port]() {
			mDspServer->processFrameData(port);
		}, Qt::QueuedConnection);
	});

	mActivePorts.insert(port);
	QLOG_INFO() << "VideoSensorManager: camera acquired for port" << port;

	activateDsp(port, activation.algo, activation.args, activation.videoOut);
}

void VideoSensorManager::activateDsp(const QString &port, trikDsp::Algorithm algo,
                                     trikDsp::InArgs args, bool videoOut)
{
	// The DSP CV algorithms must know the actual pixel format (NV16 vs YUYV) and
	// the actual bytes-per-line to decode the frame correctly. They are taken
	// from the CameraManager (cached at acquire), not from the raw device.
	mDspServer->activate({port, algo, args, videoOut,
	                      mCameraManager->width(port), mCameraManager->height(port),
	                      mCameraManager->format(port), mCameraManager->lineLength(port)});
}

void VideoSensorManager::handleStopRequested(const QString &port, bool deinit)
{
	mDspServer->deactivate();

	// A stop cancels any acquire still in flight for this port; the released
	// camera is closed by the manager's queued release() even if the acquire
	// later completes (refcount is preserved by the manager's event order).
	mPendingActivations.remove(port);

	if (deinit) {
		mCameraManager->unsubscribe(port, this);
		mCameraManager->release(port);
		mActivePorts.remove(port);
	}
}

void VideoSensorManager::create(const QString &port, const QString &deviceClass)
{
	if (!checkManagerState("create")) return;
	createSensor(port, deviceClass);
}

void VideoSensorManager::stop()
{
	if (!checkManagerState("stop")) return;

	mDspServer->deactivate();
	mPendingActivations.clear();
	mActivePorts.clear();
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

void VideoSensorManager::onResult(const QString &sourceId, trikDsp::Algorithm algorithm, trikDsp::OutArgs result)
{
	// The frame has been consumed: return the V4L2 buffer to the driver.
	mCameraManager->releaseFrame(sourceId);

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
	default: break;
	}
}

}

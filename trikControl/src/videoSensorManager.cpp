#include "videoSensorManager.h"

#include <memory>

#include <trikKernel/configurer.h>
#include <QsLog.h>

#include <trikHal/VideoDeviceFileInterface.h>
#include <trikHal/hardwareAbstractionInterface.h>
#include <trikDsp/dspTypes.h>

#include "configurerHelper.h"

namespace trikControl {

namespace {

constexpr uint16_t DSP_RPROC_ID = 0;

}

VideoSensorManager::VideoSensorManager(const trikKernel::Configurer &configurer,
				       const trikHal::HardwareAbstractionInterface &hardwareAbstraction)
	: mConfigurer(configurer)
	, mHardwareAbstractionInterface(hardwareAbstraction)
	, mState("VideoSensorManager")
{
	mDspServer.reset(new trikDsp::DspServer(DSP_RPROC_ID));
	connect(mDspServer.data(), &trikDsp::DspServer::resultReady,this, &VideoSensorManager::onResult);
	connect(mDspServer.data(), &trikDsp::DspServer::videoFrameReady, this, &VideoSensorManager::videoFrameReady);
	connect(mDspServer.data(), &trikDsp::DspServer::videoDisplayStarted, this, &VideoSensorManager::videoDisplayStarted);
	connect(mDspServer.data(), &trikDsp::DspServer::videoDisplayFinished, this, &VideoSensorManager::videoDisplayFinished);
	mState.ready();
}

VideoSensorManager::~VideoSensorManager()
{
	qDeleteAll(mLineSensors);
	qDeleteAll(mColorSensors);
	qDeleteAll(mObjectSensors);
	mDspServer.reset();
	qDeleteAll(mSources);
}

bool VideoSensorManager::ensureSourceOpened(const QString &port)
{
	if (!mState.isReady()) {
		return false;
	}

	auto &&source = mSources.value(port);

	if (!source || !source->isOpen()) {
		if (source)
			QLOG_ERROR() << "VideoSensorManager: failed to open" << source->id();
		else
			QLOG_ERROR() << "VideoSensorManager: no source for port" << port;
		mState.fail();
		return false;
	}

	if (!mDspServer->addSource(source)) {
		QLOG_ERROR() << "VideoSensorManager: failed to register" << source->id();
		source->close();
		mState.fail();
		return false;
	}

	return true;
}

void VideoSensorManager::closeSource(const QString &port)
{
	auto *source = mSources.value(port);
	if (!source || !source->isOpen())
		return;

	mDspServer->removeSource(source);
	source->close();
}

void VideoSensorManager::activateForPort(const QString &port, trikDsp::Algorithm algo,
                                         trikDsp::InArgs args, bool videoOut, bool canOpen)
{
	if (canOpen) {
		if (!ensureSourceOpened(port))
			return;
	} else {
		auto *source = mSources.value(port);
		if (!source || !source->isOpen())
			return;
	}

	mDspServer->activate({mSources[port], algo, args, videoOut});
}

void VideoSensorManager::handleStopRequested(const QString &port, bool deinit)
{
	mDspServer->deactivate();

	if (deinit) {
		closeSource(port);
	}
}

void VideoSensorManager::create(const QString &port, const QString &deviceClass)
{
	const auto devFile = mConfigurer.attributeByPort(port, "device");
	const auto fmtStr = mConfigurer.attributeByPort(port, "format");

	if (devFile.isEmpty()) {
		mState.fail();
		QLOG_ERROR() << "VideoSensorManager: no device for port" << port;
		return;
	}

	const auto w = ConfigurerHelper::configureInt(mConfigurer, mState, port, "width");
	const auto h = ConfigurerHelper::configureInt(mConfigurer, mState, port, "height");

	if (!mState.isReady()) {
		return;
	}

	auto &&src = mHardwareAbstractionInterface.createVideoDeviceFile(
				devFile, static_cast<uint32_t>(w), static_cast<uint32_t>(h), trikKernel::toV4l2Fourcc(trikDsp::pixelFormatFromString(fmtStr)));
	if (!src->open()) {
		QLOG_ERROR() << "VideoSensorManager: failed to open" << devFile;
		mState.fail();
		delete src;
		return;
	}
	mSources.insert(port, src);

	if (deviceClass == QStringLiteral("lineSensor")) {
		auto &&s = new LineSensor(port, mConfigurer);
		connect(s, &LineSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Line, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &LineSensor::stopRequested, this,
		        [this, port](bool deinit) {
			handleStopRequested(port, deinit);
		}, Qt::QueuedConnection);
		mLineSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("colorSensor")) {
		auto &&s = new ColorSensor(port, mConfigurer);
		connect(s, &ColorSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Mxn, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ColorSensor::stopRequested, this,
		        [this, port](bool deinit) {
			handleStopRequested(port, deinit);
		}, Qt::QueuedConnection);
		mColorSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("objectSensor")) {
		auto &&s = new ObjectSensor(port, mConfigurer);
		connect(s, &ObjectSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Object, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ObjectSensor::stopRequested, this,
		        [this, port](bool deinit) {
			handleStopRequested(port, deinit);
		}, Qt::QueuedConnection);
		mObjectSensors.insert(port, s);
	}
}

void VideoSensorManager::shutdown(const QString &port)
{
	mDspServer->deactivate();

	if (auto *src = mSources.value(port)) {
		mDspServer->removeSource(src);
	}

	{
		auto it = mSources.find(port);
		if (it != mSources.end()) {
			delete it.value();
			mSources.erase(it);
		}
	}

	{
		auto it = mLineSensors.find(port);
		if (it != mLineSensors.end()) {
			delete it.value();
			mLineSensors.erase(it);
		}
	}
	{
		auto it = mColorSensors.find(port);
		if (it != mColorSensors.end()) {
			delete it.value();
			mColorSensors.erase(it);
		}
	}
	{
		auto it = mObjectSensors.find(port);
		if (it != mObjectSensors.end()) {
			delete it.value();
			mObjectSensors.erase(it);
		}
	}
}

void VideoSensorManager::stop()
{
	mDspServer->deactivate();

	for (auto *s : mLineSensors) s->stop(false);
	for (auto *s : mColorSensors) s->stop(false);
	for (auto *s : mObjectSensors) s->stop(false);
}

QString VideoSensorManager::deviceClass() const
{
	return QStringLiteral("dspSensor");
}

QString VideoSensorManager::deviceToPort(const QString &device) const
{
	return device;
}

bool VideoSensorManager::isVideoSensor(const QString &deviceClass) const
{
	return deviceClass == QStringLiteral("lineSensor")
	       || deviceClass == QStringLiteral("colorSensor")
	       || deviceClass == QStringLiteral("objectSensor")
	       || deviceClass == QStringLiteral("dspSensor");
}

LineSensorInterface *VideoSensorManager::lineSensor(const QString &port)
{
	auto it = mLineSensors.find(port);
	return it == mLineSensors.end() ? nullptr : *it;
}

ColorSensorInterface *VideoSensorManager::colorSensor(const QString &port)
{
	auto it = mColorSensors.find(port);
	return it == mColorSensors.end() ? nullptr : *it;
}

ObjectSensorInterface *VideoSensorManager::objectSensor(const QString &port)
{
	auto it = mObjectSensors.find(port);
	return it == mObjectSensors.end() ? nullptr : *it;
}

void VideoSensorManager::onResult(const QString &sourceId,
                                  trikDsp::Algorithm algorithm,
                                  trikDsp::OutArgs result)
{
	QString port;
	for (auto it = mSources.begin(); it != mSources.end(); ++it) {
		if (it.value()->id() == sourceId) {
			port = it.key();
			break;
		}
	}

	if (port.isEmpty()) {
		return;
	}

	switch (algorithm) {
	case trikDsp::Algorithm::Line: {
		auto it = mLineSensors.find(port);
		if (it != mLineSensors.end())
			it.value()->onResult(result);
		break;
	}
	case trikDsp::Algorithm::Object: {
		auto it = mObjectSensors.find(port);
		if (it != mObjectSensors.end())
			it.value()->onResult(result);
		break;
	}
	case trikDsp::Algorithm::Mxn: {
		auto it = mColorSensors.find(port);
		if (it != mColorSensors.end())
			it.value()->onResult(result);
		break;
	}
	default:
		break;
	}
}

}

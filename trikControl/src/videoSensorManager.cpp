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

namespace {

constexpr uint16_t DSP_RPROC_ID = 0;

}

VideoSensorManager::VideoSensorManager(const trikKernel::Configurer &configurer,
				       const trikHal::HardwareAbstractionInterface &hardwareAbstraction)
	: mConfigurer(configurer)
	, mHardwareAbstractionInterface(hardwareAbstraction)
	, mState("VideoSensorManager")
{
	qRegisterMetaType<trikDsp::InArgs>();
	qRegisterMetaType<trikDsp::OutArgs>();
	qRegisterMetaType<trikDsp::Algorithm>();
	qRegisterMetaType<uint32_t>();

	mDspThread.reset(new QThread);
	mDspThread->setObjectName(QStringLiteral("DspServer"));

	mDspServer.reset(new trikDsp::DspServer(DSP_RPROC_ID));

	connect(mDspServer.data(), &trikDsp::DspServer::errorOccurred, this, [this](const QString &message) {
		QLOG_ERROR() << "The VideoSensorManager constructor terminated with an error:"
			     << message;
		mState.fail();
	});

	connect(mDspServer.data(), &trikDsp::DspServer::successfullyInited, this, [this]() {
		mState.ready();
	});

	connect(mDspServer.data(), &trikDsp::DspServer::resultReady,this, &VideoSensorManager::onResult);
	connect(mDspServer.data(), &trikDsp::DspServer::videoFrameReady, this, &VideoSensorManager::videoFrameReady);
	connect(mDspServer.data(), &trikDsp::DspServer::videoDisplayStarted, this, &VideoSensorManager::videoDisplayStarted);
	connect(mDspServer.data(), &trikDsp::DspServer::videoDisplayFinished, this, &VideoSensorManager::videoDisplayFinished);

	mDspServer->init();

	if (!mState.isReady())
		return;

	mDspServer->moveToThread(mDspThread.data());
	mDspThread->start();
}

void VideoSensorManager::destroyDsp()
{
	mDspThread->quit();
	mDspThread->wait();
	mDspServer.reset();
}

VideoSensorManager::~VideoSensorManager()
{
	destroyDsp();

	qDeleteAll(mLineSensors);
	qDeleteAll(mColorSensors);
	qDeleteAll(mObjectSensors);
	mLineSensors.clear();
	mColorSensors.clear();
	mObjectSensors.clear();

	qDeleteAll(mSources);
	mSources.clear();
	mPortStatuses.clear();
}

bool VideoSensorManager::checkManagerState(const QString &message) const
{
	if (!mState.isReady()) {
		QLOG_ERROR() << QString("An attempt to %1 a device on an uninitialized VideoSensorManager")
					.arg(message);
		return false;
	}
	return true;
}

void VideoSensorManager::createSensor(const QString &port, const QString &deviceClass)
{
	if (deviceClass == QStringLiteral("lineSensor")) {
		auto *s = new LineSensor(port, mConfigurer);
		connect(s, &LineSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Line, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &LineSensor::stopRequested, this,
		        [this, port](bool deinit) { handleStopRequested(port, deinit); },
		        Qt::QueuedConnection);
		mLineSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("objectSensor")) {
		auto *s = new ObjectSensor(port, mConfigurer);
		connect(s, &ObjectSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Object, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ObjectSensor::stopRequested, this,
		        [this, port](bool deinit) { handleStopRequested(port, deinit); },
		        Qt::QueuedConnection);
		mObjectSensors.insert(port, s);
	} else if (deviceClass == QStringLiteral("colorSensor")) {
		auto *s = new ColorSensor(port, mConfigurer);
		connect(s, &ColorSensor::activateRequested, this,
		        [this, port](trikDsp::InArgs args, bool videoOut, bool canOpen) {
			activateForPort(port, trikDsp::Algorithm::Mxn, args, videoOut, canOpen);
		}, Qt::QueuedConnection);
		connect(s, &ColorSensor::stopRequested, this,
		        [this, port](bool deinit) { handleStopRequested(port, deinit); },
		        Qt::QueuedConnection);
		mColorSensors.insert(port, s);
	}
}

bool VideoSensorManager::openSource(const QString &port)
{
	auto *source = mSources.value(port);
	if (!source) {
		QLOG_ERROR() << "VideoSensorManager: no source for port" << port;
		mPortStatuses[port] = PortStatus::Stopped;
		return false;
	}

	if (!source->isOpen() && !source->open()) {
		QLOG_ERROR() << "VideoSensorManager: failed to open" << source->id();
		mPortStatuses[port] = PortStatus::Stopped;
		return false;
	}

	if (!source->startStreaming()) {
		QLOG_ERROR() << "VideoSensorManager: startStreaming failed for" << source->id();
		mPortStatuses[port] = PortStatus::Stopped;
		return false;
	}
	QLOG_INFO() << "VideoSensorManager: streaming started for" << source->id();

	if (!mDspServer->addSource(source)) {
		QLOG_ERROR() << "VideoSensorManager: failed to register" << source->id();
		source->stopStreaming();
		mPortStatuses[port] = PortStatus::Stopped;
		return false;
	}

	mPortStatuses[port] = PortStatus::Ready;
	return true;
}

void VideoSensorManager::closeSource(const QString &port)
{
	if (mPortStatuses.value(port) != PortStatus::Ready)
		return;

	auto *source = mSources.value(port);
	if (!source) {
		return;
	}

	mDspServer->removeSource(source);
	source->stopStreaming();
	QLOG_INFO() << "VideoSensorManager: streaming stopped for" << source->id();
	source->close();
	QLOG_INFO() << "VideoSensorManager: source closed for" << source->id();
	mPortStatuses[port] = PortStatus::Stopped;
}

void VideoSensorManager::activateForPort(const QString &port, trikDsp::Algorithm algo,
                                         trikDsp::InArgs args, bool videoOut, bool canOpen)
{
	if (canOpen) {
		const auto status = mPortStatuses.value(port, PortStatus::Stopped);
		if (status == PortStatus::Stopped || status == PortStatus::Starting) {
			if (!openSource(port))
				return;
		}
	} else {
		if (mPortStatuses.value(port, PortStatus::Stopped) != PortStatus::Ready) {
			return;
		}
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
	if (!checkManagerState("create")) {
		return;
	}

	auto it = mSources.find(port);

	if (it == mSources.end()) {
		DeviceState state("dspSensor");

		const auto width = ConfigurerHelper::configureInt(mConfigurer, state, port, "width");
		const auto height = ConfigurerHelper::configureInt(mConfigurer, state, port, "height");

		QString defaultDevFile;
		QString defaultFmtStr;
		const auto devFile = mConfigurer.attributeByPort(port, "device", &defaultDevFile);
		const auto fmtStr = mConfigurer.attributeByPort(port, "format", &defaultFmtStr);

		if (state.isFailed()) {
			QLOG_ERROR() << "DspSensor does not contain a correct description of"
					" the frame width and height";
			mPortStatuses[port] = PortStatus::Stopped;
			return;
		}

		if (devFile.isEmpty() || fmtStr.isEmpty()) {
			QLOG_ERROR() << "DspSensor does not contain a correct description of"
					" the video format and the path to the device";
			mPortStatuses[port] = PortStatus::Stopped;
			return;
		}

		auto *src = mHardwareAbstractionInterface.createVideoDeviceFile(devFile,
				static_cast<uint32_t>(width), static_cast<uint32_t>(height),
				trikKernel::toV4l2Fourcc(trikDsp::pixelFormatFromString(fmtStr)));
		QLOG_INFO() << "VideoSensorManager: created source for port" << port
		            << "device" << devFile << "format" << fmtStr
		            << "fourcc" << Qt::hex << trikKernel::toV4l2Fourcc(trikDsp::pixelFormatFromString(fmtStr))
		            << "size" << width << "x" << height;
		src->moveToThread(thread());
		mSources.insert(port, src);
		mPortStatuses[port] = PortStatus::Starting;
	}

	createSensor(port, deviceClass);
}

void VideoSensorManager::shutdown(const QString &port)
{
	if (!checkManagerState("shutdown")) {
		return;
	}

	{
		auto it = mLineSensors.find(port);
		if (it != mLineSensors.end()) {
			auto *s = it.value();
			mLineSensors.erase(it);
			delete s;
		}
	}
	{
		auto it = mColorSensors.find(port);
		if (it != mColorSensors.end()) {
			auto *s = it.value();
			mColorSensors.erase(it);
			delete s;
		}
	}
	{
		auto it = mObjectSensors.find(port);
		if (it != mObjectSensors.end()) {
			auto *s = it.value();
			mObjectSensors.erase(it);
			delete s;
		}
	}

	mDspServer->deactivate();

	QMetaObject::invokeMethod(this, [this, port]() {
		closeSource(port);
	}, Qt::BlockingQueuedConnection);

	QMetaObject::invokeMethod(this, [this, port]() {
		auto it = mSources.find(port);
		if (it != mSources.end()) {
			delete it.value();
			mSources.erase(it);
		}
	}, Qt::BlockingQueuedConnection);

	mPortStatuses.remove(port);
}

void VideoSensorManager::stop()
{
	if (!checkManagerState("stop")) {
		return;
	}

	mDspServer->deactivate();

	for (const auto &port : mSources.keys()) {
		QMetaObject::invokeMethod(this, [this, port]() {
			closeSource(port);
		}, Qt::BlockingQueuedConnection);
	}
}

QString VideoSensorManager::deviceClass()
{
	return QStringLiteral("dspSensor");
}

QString VideoSensorManager::deviceToPort(const QString &device)
{
	return device;
}

bool VideoSensorManager::isVideoSensor(const QString &deviceClass)
{
	return deviceClass == QStringLiteral("lineSensor")
	    || deviceClass == QStringLiteral("objectSensor")
	    || deviceClass == QStringLiteral("colorSensor");
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

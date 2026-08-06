#include "objectSensor.h"

#include <trikKernel/configurer.h>

#include "configurerHelper.h"

using namespace trikControl;

ObjectSensor::ObjectSensor(const QString &port, const trikKernel::Configurer &configurer)
	: mState("Object Sensor on " + port)
	, mConfigurer(configurer)
	, mPort(port)
{
	mToleranceFactor = ConfigurerHelper::configureChildReal(
	                       configurer, mState, port, "objectSensor", "toleranceFactor");

	if (!mState.isFailed())
		mState.ready();
}

ObjectSensor::~ObjectSensor()
{
	emit stopped();
}

ObjectSensor::Status ObjectSensor::status() const
{
	return mState.status();
}

void ObjectSensor::init(bool showOnDisplay)
{
	if (mState.isFailed())
		return;

	mVideoOut = showOnDisplay;
	emit activateRequested(mInArgs, showOnDisplay, true);
}

void ObjectSensor::detect()
{
	if (status() == Status::ready) {
		mInArgs.autoDetect = true;
		emit activateRequested(mInArgs, mVideoOut, false);
	}
}

QVector<int> ObjectSensor::read()
{
	return mReading;
}

void ObjectSensor::stop(bool deinit)
{
	emit stopRequested(deinit);
	emit stopped();
}

QVector<int> ObjectSensor::getDetectParameters() const
{
	return mDetectParameters;
}

void ObjectSensor::onResult(trikDsp::OutArgs result)
{
	bool hsvUpdated = false;

	if (mInArgs.autoDetect) {
		mInArgs.autoDetect = false;
		mInArgs.params = result.detected;
		hsvUpdated = true;
	}

	mReading = {result.location.x, result.location.y,
	            static_cast<int>(result.location.size)};

	if (hsvUpdated) {
		mDetectParameters = {
			static_cast<int>(result.detected.hue.from),
			static_cast<int>(result.detected.hue.to),
			static_cast<int>(result.detected.saturation.from),
			static_cast<int>(result.detected.saturation.to),
			static_cast<int>(result.detected.value.from),
			static_cast<int>(result.detected.value.to)
		};
		emit activateRequested(mInArgs, mVideoOut, false);
	}
}

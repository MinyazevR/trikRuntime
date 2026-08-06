#include "lineSensor.h"

#include <trikKernel/configurer.h>

#include "configurerHelper.h"

using namespace trikControl;

LineSensor::LineSensor(const QString &port, const trikKernel::Configurer &configurer)
	: mState("Line Sensor on " + port)
	, mConfigurer(configurer)
	, mPort(port)
{
	mToleranceFactor = ConfigurerHelper::configureChildReal(
	                       configurer, mState, port, "lineSensor", "toleranceFactor");

	if (!mState.isFailed())
		mState.ready();
}

LineSensor::~LineSensor()
{
	emit stopped();
}

LineSensor::Status LineSensor::status() const
{
	return mState.status();
}

void LineSensor::init(bool showOnDisplay)
{
	if (mState.isFailed())
		return;

	mVideoOut = showOnDisplay;
	emit activateRequested(mInArgs, showOnDisplay, true);
}

void LineSensor::detect()
{
	if (status() == Status::ready) {
		mInArgs.autoDetect = true;
		emit activateRequested(mInArgs, mVideoOut, false);
	}
}

QVector<int> LineSensor::read()
{
	QReadLocker locker(&mReadingLock);
	return mReading;
}

void LineSensor::stop(bool deinit)
{
	emit stopRequested(deinit);
	emit stopped();
}

QVector<int> LineSensor::getDetectParameters() const
{
	QReadLocker locker(&mDetectParametersLock);
	return mDetectParameters;
}

void LineSensor::onResult(trikDsp::OutArgs result)
{
	bool hsvUpdated = false;

	if (mInArgs.autoDetect) {
		mInArgs.autoDetect = false;
		mInArgs.params = result.detected;
		hsvUpdated = true;
	}

	{
		QWriteLocker locker(&mReadingLock);
		mReading = {result.location.x, result.location.y,
		            static_cast<int>(result.location.size)};
	}

	if (hsvUpdated) {
		{
			QWriteLocker locker(&mDetectParametersLock);
			mDetectParameters = {
				static_cast<int>(result.detected.hue.from),
				static_cast<int>(result.detected.hue.to),
				static_cast<int>(result.detected.saturation.from),
				static_cast<int>(result.detected.saturation.to),
				static_cast<int>(result.detected.value.from),
				static_cast<int>(result.detected.value.to)
			};
		}
		emit activateRequested(mInArgs, mVideoOut, false);
	}
}

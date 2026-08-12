#include "objectSensor.h"

#include <trikKernel/configurer.h>

#include "configurerHelper.h"

using namespace trikControl;

ObjectSensor::ObjectSensor(const QString &port, const trikKernel::Configurer &configurer)
	: m("Object Sensor on " + port, configurer, port, trikDsp::Algorithm::Object)
{
	mToleranceFactor = ConfigurerHelper::configureChildReal(
	                       configurer, m.state(), port, "objectSensor", "toleranceFactor");

	if (!m.state().isFailed())
		m.state().ready();
}

ObjectSensor::~ObjectSensor()
{
	Q_EMIT stopped();
}

ObjectSensor::Status ObjectSensor::status() const
{
	return m.state().status();
}

void ObjectSensor::init(bool showOnDisplay)
{
	if (!m.doInit(showOnDisplay))
		return;

	Q_EMIT activateRequested(m.inArgs(), showOnDisplay, true);
}

void ObjectSensor::detect()
{
	if (status() == Status::ready) {
		m.inArgs().autoDetect = true;
		Q_EMIT activateRequested(m.inArgs(), m.videoOut(), false);
	}
}

QVector<int> ObjectSensor::read()
{
	QReadLocker locker(&mReadingLock);
	return mReading;
}

void ObjectSensor::stop(bool deinit)
{
	m.doStop();
	Q_EMIT stopRequested(deinit);
	Q_EMIT stopped();
}

QVector<int> ObjectSensor::getDetectParameters() const
{
	QReadLocker locker(&mDetectParametersLock);
	return mDetectParameters;
}

void ObjectSensor::onResult(trikDsp::OutArgs result)
{
	bool hsvUpdated = false;

	if (m.inArgs().autoDetect) {
		m.inArgs().autoDetect = false;
		m.inArgs().params = result.detected;
		applyToleranceFactor(m.inArgs().params, mToleranceFactor);
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
			mDetectParameters = toDetectParameters(result.detected);
		}
		Q_EMIT activateRequested(m.inArgs(), m.videoOut(), false);
	}
}


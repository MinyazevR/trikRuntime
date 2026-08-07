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
	return mDetectParameters;
}

void ObjectSensor::onResult(trikDsp::OutArgs result)
{
	bool hsvUpdated = false;

	if (m.inArgs().autoDetect) {
		m.inArgs().autoDetect = false;
		m.inArgs().params = result.detected;
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
		Q_EMIT activateRequested(m.inArgs(), m.videoOut(), false);
	}
}


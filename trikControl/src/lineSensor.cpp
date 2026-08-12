#include "lineSensor.h"

#include <trikKernel/configurer.h>

#include "configurerHelper.h"

using namespace trikControl;

LineSensor::LineSensor(const QString &port, const trikKernel::Configurer &configurer)
	: m("Line Sensor on " + port, configurer, port, trikDsp::Algorithm::Line)
{
	mToleranceFactor = ConfigurerHelper::configureChildReal(
	                       configurer, m.state(), port, "lineSensor", "toleranceFactor");

	if (!m.state().isFailed())
		m.state().ready();
}

LineSensor::~LineSensor()
{
	Q_EMIT stopped();
}

LineSensor::Status LineSensor::status() const
{
	return m.state().status();
}

void LineSensor::init(bool showOnDisplay)
{
	if (!m.doInit(showOnDisplay))
		return;

	Q_EMIT activateRequested(m.inArgs(), showOnDisplay, true);
}

void LineSensor::detect()
{
	if (status() == Status::ready) {
		m.inArgs().autoDetect = true;
		Q_EMIT activateRequested(m.inArgs(), m.videoOut(), false);
	}
}

QVector<int> LineSensor::read()
{
	QReadLocker locker(&mReadingLock);
	return mReading;
}

void LineSensor::stop(bool deinit)
{
	m.doStop();
	Q_EMIT stopRequested(deinit);
	Q_EMIT stopped();
}

QVector<int> LineSensor::getDetectParameters() const
{
	QReadLocker locker(&mDetectParametersLock);
	return mDetectParameters;
}

void LineSensor::onResult(trikDsp::OutArgs result)
{
	bool hsvUpdated = false;

	if (m.inArgs().autoDetect) {
		m.inArgs().autoDetect = false;
		// Feed the detected range back to the DSP, widening it by the config
		// toleranceFactor (as the old worker did before re-sending).
		m.inArgs().params = result.detected;
		applyToleranceFactor(m.inArgs().params, mToleranceFactor);
		hsvUpdated = true;
	}

	// The DSP Line algorithm reports crossroads probability in `y` and the
	// line "mass" in `size` (see line_sensor.hpp): {x, crossroads, mass}.
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


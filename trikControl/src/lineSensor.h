#pragma once

#include <QtCore/QReadWriteLock>
#include <QtCore/QVector>

#include <trikDsp/dspTypes.h>

#include "lineSensorInterface.h"
#include "dspSensorBase.h"

namespace trikKernel { class Configurer; }

namespace trikControl {

class LineSensor : public LineSensorInterface
{
	Q_OBJECT

public:
	LineSensor(const QString &port, const trikKernel::Configurer &configurer);
	~LineSensor() override;

	Status status() const override;
	Q_INVOKABLE QVector<int> read() override;
	Q_INVOKABLE QVector<int> getDetectParameters() const override;

	void onResult(trikDsp::OutArgs result);

Q_SIGNALS:
	void activateRequested(trikDsp::InArgs args, bool videoOut, bool canOpen);
	void stopRequested(bool deinit);

public Q_SLOTS:
	void init(bool showOnDisplay) override;
	void detect() override;
	void stop(bool deinit = true) override;

private:
	DspSensorHelper m;
	qreal mToleranceFactor = 1.0;

	QVector<int> mReading{0, 0, 0};
	QVector<int> mDetectParameters{0, 0, 0, 0, 0, 0};
	mutable QReadWriteLock mReadingLock;
	mutable QReadWriteLock mDetectParametersLock;
};

}


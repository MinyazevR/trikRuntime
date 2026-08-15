#pragma once

#include <QtCore/QReadWriteLock>
#include <QtCore/QVector>

#include <trikDsp/dspTypes.h>

#include "objectSensorInterface.h"
#include "dspSensorBase.h"

namespace trikKernel { class Configurer; }

namespace trikControl {

class ObjectSensor : public ObjectSensorInterface
{
	Q_OBJECT

public:
	ObjectSensor(const QString &port, const trikKernel::Configurer &configurer);
	~ObjectSensor() override;

	Status status() const override;
	void onResult(trikDsp::OutArgs result);

	QVector<int> read() override;
	QVector<int> getDetectParameters() const override;

Q_SIGNALS:
	void activateRequested(trikDsp::InArgs args, bool videoOut, bool canOpen);
	void stopRequested(int flags);

public Q_SLOTS:
	void init(bool showOnDisplay) override;
	void detect() override;
	void stop(int flags = StopAll) override;

private:
	DspSensorHelper m;
	qreal mToleranceFactor = 1.0;

	QVector<int> mReading{0, 0, 0};
	QVector<int> mDetectParameters{0, 0, 0, 0, 0, 0};
	mutable QReadWriteLock mReadingLock;
	mutable QReadWriteLock mDetectParametersLock;
};

}


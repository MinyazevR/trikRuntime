#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <trikDsp/dspTypes.h>

#include "objectSensorInterface.h"
#include "deviceState.h"

namespace trikKernel { class Configurer; }

namespace trikControl {

/// Object-detection sensor.  Same pattern as LineSensor —
/// emits activateRequested, receives results via onResult().
class ObjectSensor : public ObjectSensorInterface
{
	Q_OBJECT

public:
	ObjectSensor(const QString &port, const trikKernel::Configurer &configurer);
	~ObjectSensor() override;

	Status status() const override;
	void onResult(trikDsp::OutArgs result);

Q_SIGNALS:
	void activateRequested(trikDsp::InArgs args, bool videoOut, bool canOpen);
	void stopRequested(bool deinit);

public Q_SLOTS:
	void init(bool showOnDisplay) override;
	void detect() override;
	QVector<int> read() override;
	void stop(bool deinit = true) override;
	QVector<int> getDetectParameters() const override;

private:
	DeviceState mState;
	const trikKernel::Configurer &mConfigurer;
	const QString mPort;
	qreal mToleranceFactor = 1.0;

	trikDsp::InArgs mInArgs;
	bool mVideoOut = false;

	QVector<int> mReading{0, 0, 0};
	QVector<int> mDetectParameters{0, 0, 0, 0, 0, 0};
};

}

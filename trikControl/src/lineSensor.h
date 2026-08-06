#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QReadWriteLock>
#include <QtCore/QVector>

#include <trikDsp/dspTypes.h>

#include "lineSensorInterface.h"
#include "deviceState.h"

namespace trikKernel { class Configurer; }

namespace trikControl {

/// Line-following sensor.  Emits activateRequested(InArgs,bool) to
/// wire into the DSP pipeline; receives results via onResult().
/// Owns no hardware resources — VideoSensorManager manages the source.
class LineSensor : public LineSensorInterface
{
	Q_OBJECT

public:
	LineSensor(const QString &port, const trikKernel::Configurer &configurer);
	~LineSensor() override;

	Status status() const override;
	Q_INVOKABLE QVector<int> read() override;
	Q_INVOKABLE QVector<int> getDetectParameters() const override;

	/// Called by VideoSensorManager with a DSP result for this sensor.
	void onResult(trikDsp::OutArgs result);

Q_SIGNALS:
	/// Emitted when the sensor wants to (re)activate the DSP pipeline.
	/// @param canOpen  true for init() — VSM will open the source if needed.
	///                 false for detect()/autoDetect — fails if not open.
	void activateRequested(trikDsp::InArgs args, bool videoOut, bool canOpen);

	/// Emitted from stop().  deinit=true means release hardware resources
	/// (close V4L2, close framebuffer); false means pause only.
	void stopRequested(bool deinit);

public Q_SLOTS:
	void init(bool showOnDisplay) override;
	void detect() override;
	void stop(bool deinit = true) override;

private:
	DeviceState mState;
	const trikKernel::Configurer &mConfigurer;
	const QString mPort;
	qreal mToleranceFactor = 1.0;

	trikDsp::InArgs mInArgs;
	bool mVideoOut = false;

	QVector<int> mReading{0, 0, 0};
	QVector<int> mDetectParameters{0, 0, 0, 0, 0, 0};
	mutable QReadWriteLock mReadingLock;
	mutable QReadWriteLock mDetectParametersLock;
};

}

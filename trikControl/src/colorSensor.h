#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <trikDsp/dspTypes.h>

#include "colorSensorInterface.h"
#include "deviceState.h"

namespace trikKernel { class Configurer; }

namespace trikControl {

/// MxN colour-grid sensor.  DSP divides the frame into mM × mN cells
/// and returns the dominant colour per cell.  Emits activateRequested
/// on init(); receives results via onResult().
class ColorSensor : public ColorSensorInterface
{
	Q_OBJECT

public:
	ColorSensor(const QString &port, const trikKernel::Configurer &configurer);
	~ColorSensor() override;

	Status status() const override;
	void onResult(trikDsp::OutArgs result);

Q_SIGNALS:
	void activateRequested(trikDsp::InArgs args, bool videoOut, bool canOpen);
	void stopRequested(bool deinit);

public Q_SLOTS:
	void init(bool showOnDisplay) override;
	QVector<int> read(int m, int n) override;
	void stop(bool deinit = true) override;

private:
	DeviceState mState;
	const trikKernel::Configurer &mConfigurer;
	const QString mPort;

	trikDsp::InArgs mInArgs;
	bool mVideoOut = false;

	QVector<QVector<QVector<int>>> mReading;
	int mM = 0;
	int mN = 0;
};

}

#pragma once

#include <QtCore/QVector>

#include <trikDsp/dspTypes.h>

#include "colorSensorInterface.h"
#include "dspSensorBase.h"

namespace trikKernel { class Configurer; }

namespace trikControl {

class ColorSensor : public ColorSensorInterface
{
	Q_OBJECT

public:
	ColorSensor(const QString &port, const trikKernel::Configurer &configurer);
	~ColorSensor() override;

	Status status() const override;
	void onResult(trikDsp::OutArgs result);

	QVector<int> read(int m, int n) override;

Q_SIGNALS:
	void activateRequested(trikDsp::InArgs args, bool videoOut, bool canOpen);
	void stopRequested(bool deinit);

public Q_SLOTS:
	void init(bool showOnDisplay) override;
	void stop(bool deinit = true) override;

private:
	DspSensorHelper m;

	QVector<QVector<QVector<int>>> mReading;
	int mM = 0;
	int mN = 0;
};

}


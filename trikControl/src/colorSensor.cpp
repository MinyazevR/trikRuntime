#include "colorSensor.h"

#include <algorithm>

#include <trikKernel/configurer.h>

#include "configurerHelper.h"

using namespace trikControl;

ColorSensor::ColorSensor(const QString &port, const trikKernel::Configurer &configurer)
	: m("Color Sensor on " + port, configurer, port, trikDsp::Algorithm::Mxn)
{
	mM = ConfigurerHelper::configureInt(configurer, m.state(), port, "m");
	mN = ConfigurerHelper::configureInt(configurer, m.state(), port, "n");

	if (mM <= 0 || mN <= 0 || m.state().isFailed()) {
		m.state().fail();
		return;
	}

	m.state().ready();

	m.inArgs().m = mM;
	m.inArgs().n = mN;

	mReading.resize(mM);
	for (int i = 0; i < mM; ++i) {
		mReading[i].resize(mN);
		for (int j = 0; j < mN; ++j)
			mReading[i][j] = {0, 0, 0};
	}
}

ColorSensor::~ColorSensor()
{
	Q_EMIT stopped();
}

ColorSensor::Status ColorSensor::status() const
{
	return m.state().status();
}

void ColorSensor::init(bool showOnDisplay)
{
	if (!m.doInit(showOnDisplay))
		return;

	Q_EMIT activateRequested(m.inArgs(), showOnDisplay, true);
}

QVector<int> ColorSensor::read(int mIdx, int nIdx)
{
	if (mIdx > mReading.size() || nIdx > mReading[0].size() || mIdx <= 0 || nIdx <= 0) {
		QLOG_WARN() << QString("Incorrect parameters for read: m = %1, n = %2").arg(mIdx).arg(nIdx);
		return {-1, -1, -1};
	}

	return mReading[mIdx - 1][nIdx - 1];
}

void ColorSensor::stop(bool deinit)
{
	m.doStop();
	Q_EMIT stopRequested(deinit);
	Q_EMIT stopped();
}

void ColorSensor::onResult(trikDsp::OutArgs result)
{
	const int total = std::min(mM * mN, 9);
	for (int i = 0; i < total; ++i) {
		const int row = i / mN;
		const int col = i % mN;
		const uint32_t c = result.colors[i];
		mReading[row][col] = {
			static_cast<int>((c >> 16) & 0xFF),
			static_cast<int>((c >> 8) & 0xFF),
			static_cast<int>(c & 0xFF)
		};
	}
}


#include "colorSensor.h"

#include <algorithm>

#include <trikKernel/configurer.h>

#include "configurerHelper.h"

using namespace trikControl;

ColorSensor::ColorSensor(const QString &port, const trikKernel::Configurer &configurer)
	: mState("Color Sensor on " + port)
	, mConfigurer(configurer)
	, mPort(port)
{
	mM = ConfigurerHelper::configureInt(configurer, mState, port, "m");
	mN = ConfigurerHelper::configureInt(configurer, mState, port, "n");

	if (mM <= 0 || mN <= 0 || mState.isFailed()) {
		mState.fail();
		return;
	}

	mState.ready();

	mInArgs.m = mM;
	mInArgs.n = mN;

	mReading.resize(mM);
	for (int i = 0; i < mM; ++i) {
		mReading[i].resize(mN);
		for (int j = 0; j < mN; ++j)
			mReading[i][j] = {0, 0, 0};
	}
}

ColorSensor::~ColorSensor()
{
	emit stopped();
}

ColorSensor::Status ColorSensor::status() const
{
	return mState.status();
}

void ColorSensor::init(bool showOnDisplay)
{
	if (mState.isFailed())
		return;

	mVideoOut = showOnDisplay;
	emit activateRequested(mInArgs, showOnDisplay, true);
}

QVector<int> ColorSensor::read(int m, int n)
{
	if (m > mReading.size() || n > mReading[0].size() || m <= 0 || n <= 0) {
		QLOG_WARN() << QString("Incorrect parameters for read: m = %1, n = %2").arg(m).arg(n);
		return {-1, -1, -1};
	}

	return mReading[m - 1][n - 1];
}

void ColorSensor::stop(bool deinit)
{
	emit stopRequested(deinit);
	emit stopped();
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

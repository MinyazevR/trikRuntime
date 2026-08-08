#include "dspSensorBase.h"

#include <trikKernel/configurer.h>
#include <QsLog.h>

namespace trikControl {

DspSensorHelper::DspSensorHelper(const QString &name, const trikKernel::Configurer &configurer,
                                 const QString &port, trikDsp::Algorithm algo)
	: mState(name)
	, mConfigurer(configurer)
	, mPort(port)
	, mAlgo(algo)
{
}

bool DspSensorHelper::doInit(bool showOnDisplay)
{
	if (mState.isFailed()) {
		QLOG_ERROR() << "An attempt to start the sensor in a failed state: " << mPort;
		return false;
	}

	mVideoOut = showOnDisplay;
	return true;
}

void DspSensorHelper::doStop()
{
	mVideoOut = false;
}

}


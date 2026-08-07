#pragma once

#include <trikDsp/dspTypes.h>

#include "deviceState.h"

namespace trikKernel {
class Configurer;
}

namespace trikControl {

class DspSensorHelper final
{
public:
	DspSensorHelper(const QString &name, const trikKernel::Configurer &configurer,
	                const QString &port, trikDsp::Algorithm algo);

	trikDsp::Algorithm algorithm() const { return mAlgo; }

	DeviceState &state() { return mState; }
	const DeviceState &state() const { return mState; }
	const trikKernel::Configurer &configurer() const { return mConfigurer; }
	const QString &port() const { return mPort; }

	trikDsp::InArgs &inArgs() { return mInArgs; }
	const trikDsp::InArgs &inArgs() const { return mInArgs; }
	bool videoOut() const { return mVideoOut; }

	bool doInit(bool showOnDisplay);
	void doStop();

private:
	DeviceState mState;
	const trikKernel::Configurer &mConfigurer;
	const QString mPort;
	trikDsp::Algorithm mAlgo;
	trikDsp::InArgs mInArgs;
	bool mVideoOut = false;
};

}


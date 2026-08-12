#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QScopedPointer>
#include <QtCore/QString>
#include <QtCore/QThread>

#include <QtCore/QSet>

#include <trikDsp/dspServer.h>

#include "lineSensorInterface.h"
#include "objectSensorInterface.h"
#include "colorSensorInterface.h"
#include "lineSensor.h"
#include "objectSensor.h"
#include "colorSensor.h"
#include "deviceState.h"
#include "cameraManager.h"

namespace trikHal {
class HardwareAbstractionInterface;
}

namespace trikKernel {
class Configurer;
}

namespace trikControl {

/// Manages DSP-based video sensors across multiple camera ports.
///
/// Owns one DspServer (DSP bridge), one CameraManager (shared V4L2 devices),
/// and per-port sensor instances.  Camera lifecycle (open/close/refcount)
/// is delegated to CameraManager — VSM only manages DSP channel logic.
///
/// ## Algorithm switching
///
/// stop(deinit=false): disconnect DSP signals, camera stays streaming.
/// Next init() skips open and reconnects — near-instant.
/// stop(deinit=true): disconnect + release camera via CameraManager.
///
/// ## Threading
///
/// CameraManager lives in the same thread as VSM (GUI/main).
/// DspServer lives in mDspThread.
/// Sensor signals → VSM slots via QueuedConnection.
class VideoSensorManager : public QObject
{
	Q_OBJECT
public:
	explicit VideoSensorManager(const trikKernel::Configurer &configurer,
				    const trikHal::HardwareAbstractionInterface &hardwareAbstraction,
				    CameraManager *cameraManager);
	~VideoSensorManager() override;

	LineSensorInterface *lineSensor(const QString &port);
	ObjectSensorInterface *objectSensor(const QString &port);
	ColorSensorInterface *colorSensor(const QString &port);

	void stop();
	void create(const QString &port, const QString &deviceClass);

	static bool isVideoSensor(const QString &deviceClass);

Q_SIGNALS:
	void videoDisplayStarted();
	void videoDisplayFinished();

private Q_SLOTS:
	void onResult(const QString &sourceId,
	              trikDsp::Algorithm algorithm,
	              trikDsp::OutArgs result);

private:
	void activateForPort(const QString &port, trikDsp::Algorithm algo,
	                     trikDsp::InArgs args, bool videoOut, bool canOpen);
	void handleStopRequested(const QString &port, bool deinit);
	void createSensor(const QString &port, const QString &deviceClass);
	bool checkManagerState(const QString &message) const;
	void destroyDsp();

	const trikKernel::Configurer &mConfigurer;
	const trikHal::HardwareAbstractionInterface &mHardwareAbstractionInterface;

	DeviceState mState;

	QScopedPointer<trikDsp::DspServer> mDspServer;
	QScopedPointer<QThread> mDspThread;
	CameraManager *mCameraManager = nullptr;

	/// Active camera ports (acquired via CameraManager).
	QSet<QString> mActivePorts;

	/// Sensor instances indexed by port.
	QHash<QString, LineSensor*> mLineSensors;
	QHash<QString, ColorSensor*> mColorSensors;
	QHash<QString, ObjectSensor*> mObjectSensors;

	QElapsedTimer mCameraFpsTimer;
	int mCameraFrameCount = 0;
};

}

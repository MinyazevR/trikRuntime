#pragma once

#include <QtCore/QHash>
#include <QtCore/QScopedPointer>
#include <QtCore/QString>
#include <QtCore/QThread>

#include <trikDsp/dspServer.h>

#include <trikHal/VideoDeviceFileInterface.h>

#include "lineSensorInterface.h"
#include "objectSensorInterface.h"
#include "colorSensorInterface.h"
#include "lineSensor.h"
#include "objectSensor.h"
#include "colorSensor.h"
#include "deviceState.h"

namespace trikHal {
class HardwareAbstractionInterface;
}

namespace trikKernel {
class Configurer;
}

namespace trikControl {

class VideoSensorManager : public QObject
{
	Q_OBJECT
public:
	explicit VideoSensorManager(const trikKernel::Configurer &configurer,
				    const trikHal::HardwareAbstractionInterface &hardwareAbstraction);
	~VideoSensorManager() override;

	LineSensorInterface *lineSensor(const QString &port);
	ObjectSensorInterface *objectSensor(const QString &port);
	ColorSensorInterface *colorSensor(const QString &port);

	void stop();
	void shutdown(const QString &port);
	void create(const QString &port, const QString &deviceClass);

	bool isVideoSensor(const QString &deviceClass) const;
	QString deviceClass() const;
	QString deviceToPort(const QString &device) const;

Q_SIGNALS:
	void videoFrameReady(const QByteArray &data,
	                     uint32_t width, uint32_t height);
	void videoDisplayStarted();
	void videoDisplayFinished();

private slots:
	void onResult(const QString &sourceId,
	              trikDsp::Algorithm algorithm,
	              trikDsp::OutArgs result);

private:
	/// Open or reuse the V4L2 source for the port.  No-op if already open.
	bool ensureSourceOpened(const QString &port);

	/// Close the V4L2 source and remove its notifier from DspServer.
	/// No-op if not open.
	void closeSource(const QString &port);

	/// Close guard → DspServer::activate.
	void activateForPort(const QString &port, trikDsp::Algorithm algo,
	                     trikDsp::InArgs args, bool videoOut, bool canOpen);

	/// Deactivate channel + optionally release hardware resources.
	void handleStopRequested(const QString &port, bool deinit);

	const trikKernel::Configurer &mConfigurer;
	const trikHal::HardwareAbstractionInterface &mHardwareAbstractionInterface;
	DeviceState mState;

	QScopedPointer<trikDsp::DspServer> mDspServer;
	QScopedPointer<QThread> mDspThread;
	QHash<QString, trikHal::VideoDeviceFileInterface*> mSources;
	QHash<QString, LineSensor*> mLineSensors;
	QHash<QString, ColorSensor*> mColorSensors;
	QHash<QString, ObjectSensor*> mObjectSensors;
};

}

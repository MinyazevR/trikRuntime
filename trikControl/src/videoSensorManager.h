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
#include "jpegEncoderSensor.h"
#include "deviceState.h"
#include "cameraManager.h"

#include <trikControl/videoSensorStopFlags.h>

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
/// stop(StopNone): deactivate the DSP, camera keeps streaming.
/// stop(StopStream): deactivate + streamoff the camera, but keep it acquired.
/// stop(StopAll): deactivate + unsubscribe + release the camera (default).
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

	/// Internal (non-Brick) JPEG encoder sensor. Creates it on first use.
	/// The caller drives it through init(jpegQuality, ifBlackAndWhite, ...).
	JpegEncoderSensor *jpegEncoderSensor(const QString &port);

	void stop(int flags = StopAll);
	void clear();
	void create(const QString &port, const QString &deviceClass);

	/// Forcibly release our hold on @p port: deactivate the DSP and forget the
	/// port (drop it from the active/pending bookkeeping) so a subsequent
	/// CameraManager::stop(port) — the caller's job — can close the device
	/// regardless of refcount. Used by Brick::startTranslation().
	void releasePort(const QString &port);

	/// Mark @p port as detached: its sensor/encoder and camera are kept alive
	/// across stop()/clear() until explicitly stopped via stopTranslation().
	void setPortDetached(const QString &port, bool detached);

	/// Stop the JPEG encoder translation on @p port (if any) and clear its
	/// detached flag. If @p keepCamera is true the camera is only streamed off
	/// (kept acquired) so a subsequent sensor init switches the DSP algorithm
	/// without reopening the device; otherwise the camera is released too.
	void stopTranslation(const QString &port, bool keepCamera);

	static bool isVideoSensor(const QString &deviceClass);

Q_SIGNALS:
	/// Emitted whenever any video sensor finishes stopping (both deinit and
	/// plain stop). Used to repaint the display so a sensor's last frame is
	/// cleared before the next sensor initializes.
	void sensorStopped();

private Q_SLOTS:
	void onResult(const QString &sourceId,
	              trikDsp::Algorithm algorithm,
	              trikDsp::OutArgs result);

	/// CameraManager::acquired() completion handler. The camera is open and
	/// streaming here; subscribe to frames and activate the DSP algorithm.
	void onAcquired(const QString &port, bool ok);

private:
	void activateForPort(const QString &port, trikDsp::Algorithm algo,
	                     trikDsp::InArgs args, bool videoOut, bool canOpen);
	void activateDsp(const QString &port, trikDsp::Algorithm algo,
	                 trikDsp::InArgs args, bool videoOut);
	/// (Re)subscribe as the camera's streaming (push) consumer. Idempotent: a
	/// StopStream may have dropped the subscription, so re-initing a parked
	/// sensor must re-subscribe before (re)activating the DSP.
	void subscribeFrames(const QString &port);
	void handleStopRequested(const QString &port, int flags);
	void createSensor(const QString &port, const QString &deviceClass);
	bool checkManagerState(const QString &message) const;
	void destroyDsp();

	const trikKernel::Configurer &mConfigurer;
	const trikHal::HardwareAbstractionInterface &mHardwareAbstractionInterface;

	DeviceState mState;

	QScopedPointer<trikDsp::DspServer> mDspServer;
	QScopedPointer<QThread> mDspThread;
	CameraManager *mCameraManager = nullptr;

	/// A DSP activation whose camera acquisition is still in flight. The latest
	/// request wins, so a detect() arriving while init()'s acquire is pending is
	/// not lost.
	struct PendingActivation {
		trikDsp::Algorithm algo;
		trikDsp::InArgs args;
		bool videoOut;
	};

	/// Ports whose translation/sensor is detached and must survive stop()/clear().
	QSet<QString> mDetachedPorts;

	/// Port currently driving the single DSP channel (empty when inactive).
	QString mActiveDspPort;

	/// Active camera ports (acquired via CameraManager).
	QSet<QString> mActivePorts;

	/// Ports whose acquire() is in flight, with the activation to run on
	/// completion. The port is removed here when the acquire finishes (or is
	/// cancelled by a stop).
	QHash<QString, PendingActivation> mPendingActivations;

	/// Sensor instances indexed by port.
	QHash<QString, LineSensor*> mLineSensors;
	QHash<QString, ColorSensor*> mColorSensors;
	QHash<QString, ObjectSensor*> mObjectSensors;
	QHash<QString, JpegEncoderSensor*> mJpegEncoders;

	QElapsedTimer mCameraFpsTimer;
	int mCameraFrameCount = 0;
};

}

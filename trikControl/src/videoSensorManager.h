/* Copyright 2026 CyberTech Labs Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#pragma once

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
/// is delegated to CameraManager - VSM only manages DSP channel logic.
///
/// ## Algorithm switching (sensor stop() flags)
///
/// A sensor's stop(StopNone) deactivates the DSP but keeps the camera streaming;
/// stop(StopStream) parks the camera (streamoff, kept open for a quick
/// re-acquire); stop(StopAll) releases the camera too (default). These flags
/// only affect per-sensor stop() (handleStopRequested);
/// VideoSensorManager::stop() is always a full teardown of every non-detached
/// camera.
///
/// ## Threading / connections
///
/// VSM (and the sensors it owns) live in the GUI/main thread. The CameraManager
/// lives in its own worker thread and the DspServer lives in mDspThread, so every
/// VSM <-> CameraManager / DspServer interaction is a queued connection:
///   - CameraManager::acquired -> onAcquired (completes an async acquire())
///   - DspServer::resultReady -> onResult (a finished DSP frame)
///   - DspServer::errorOccurred / successfullyInited -> state fail/ready
///
/// Sensor signals (activateRequested / stopRequested) are connected to VSM with an
/// explicit QueuedConnection: a sensor may emit activateRequested from inside its
/// onResult() (re-activation after detect()), and the queue prevents that from
/// re-entering the activation path synchronously.
///
/// Frame flow: CameraManager's worker thread emits frameReady(port, bufferIdx),
/// VSM forwards the buffer index to the DSP (which reads the frame directly from
/// the DSP input buffer the VPIF DMA wrote into via USERPTR), the DSP result
/// returns via resultReady -> onResult (main thread), which finally releases the
/// V4L2 buffer through CameraManager::releaseFrame(port, bufferIdx). The camera
/// streams continuously and multiple buffers may be in flight at once.
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

	/// Full teardown: deactivate the DSP (unless the active channel is detached)
	/// and stop + release every held camera except detached ports.
	void stop();
	void clear();
	void create(const QString &port, const QString &deviceClass);

	/// Forcibly release our hold on @p port: deactivate the DSP, drop the push
	/// subscription and release the camera refcount, so the port is fully free.
	/// The caller (Brick::startVideoTranslation) then hands the device to
	/// mjpg-streamer (USB) or re-acquires it for the JPEG encoder (video ports).
	void releasePort(const QString &port);

	/// Mark @p port as detached: its sensor/encoder and camera are kept alive
	/// across stop()/clear() until explicitly stopped via stopVideoTranslation().
	void setPortDetached(const QString &port, bool detached);

	/// Stop the JPEG encoder translation on @p port (if any) and clear its
	/// detached flag. If @p keepCamera is true the camera is only streamed off
	/// (kept acquired) so a subsequent sensor init switches the DSP algorithm
	/// without reopening the device; otherwise the camera is released too.
	void stopTranslation(const QString &port, bool keepCamera);

	static bool isVideoSensor(const QString &deviceClass);

Q_SIGNALS:
	/// Emitted when a video sensor stops in a way that tears the camera down
	/// (StopStream or StopAll), so a stale frame may be left on the framebuffer.
	/// The GUI repaints on this signal. A pure algorithm switch (StopNone)
	/// keeps the camera running and does not emit it.
	void sensorStopped();

private Q_SLOTS:
	void onResult(const QString &sourceId,
	              trikDsp::Algorithm algorithm,
	              const trikDsp::OutArgs &result,
	              uint32_t bufferIdx);

	/// CameraManager::acquired() completion handler. The camera is open and
	/// streaming here; activate the DSP algorithm.
	void onAcquired(const QString &port, bool ok);

private:
	void activateForPort(const QString &port, trikDsp::Algorithm algo,
	                     const trikDsp::InArgs &args, bool videoOut, bool canOpen);
	void activateDsp(const QString &port, trikDsp::Algorithm algo,
	                 const trikDsp::InArgs &args, bool videoOut);
	void handleStopRequested(const QString &port, int flags);
	void createSensor(const QString &port, const QString &deviceClass);
	bool checkManagerState(const QString &message) const;
	void destroyDsp();

	/// Latest-wins DSP feeding: if @p port has a pending (retained, not yet
	/// handed to the DSP) frame and it is not already processing, send the
	/// pending frame to the DSP. Called on frameReady and onResult. Runs in the
	/// main thread.
	void kickDsp(const QString &port);

	/// Drop and release the pending (not yet processed) frame of @p port, if
	/// any. Called when the port stops driving the DSP or is released. Runs in
	/// the main thread.
	void clearPendingFrame(const QString &port);

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
		trikDsp::Algorithm algo = trikDsp::Algorithm::None;
		trikDsp::InArgs args = {};
		bool videoOut = false;
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

	/// The latest retained frame of the DSP-active port that has not been handed
	/// to the DSP yet. A newer frameReady supersedes (releases) the previous
	/// entry, so the DSP always processes the freshest frame even if the camera
	/// outruns it. Only the DSP-active port accumulates here; other ports drop
	/// their frames immediately.
	QHash<QString, uint32_t> mPendingFrames;

	/// Ports with a processFrameData() currently in flight on the DSP thread.
	/// Used to coalesce frameReady notifications (one frame in flight + one
	/// pending per port).
	QSet<QString> mDspBusyPorts;

	/// Sensor instances indexed by port.
	QHash<QString, LineSensor*> mLineSensors;
	QHash<QString, ColorSensor*> mColorSensors;
	QHash<QString, ObjectSensor*> mObjectSensors;
	QHash<QString, JpegEncoderSensor*> mJpegEncoders;
};

}

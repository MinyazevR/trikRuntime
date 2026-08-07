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

/// Manages DSP-based video sensors across multiple camera ports.
///
/// ## Architecture
///
/// VideoSensorManager owns one DspServer (single-channel DSP bridge) and one
/// QThread.  Per camera port it allocates one V4L2 source and up to three
/// sensor instances (LineSensor, ObjectSensor, ColorSensor).  Sensors
/// communicate with VSM via activateRequested/stopRequested signals.
///
/// ## Port state machine
///
/// ```
///                create()
///   (none) ───────────────────► Starting (source allocated, not on DSP)
///                                   │
///                            openSource()
///                                   │
///                                   ▼
///   Stopped ◄── closeSource() ── Ready (source open + registered on DSP)
///     ▲         / shutdown()
///     │
///     └── stop(deinit=true) / shutdown()
/// ```
///
/// ## DSC→DSP port reuse (the "hack")
///
/// When Brick::configure() switches lineSensor→objectSensor on the same port,
/// it skips shutdown() and calls create() directly.  create() detects the
/// existing source and reuses it — only new sensor instances are created.
/// All three sensor types can coexist for one port, sharing one V4L2 source.
///
/// ## Camera resource management
///
/// - stop(deinit=true):  release camera to DspServer (removeSource), close it.
///   Next init() must reopen.
/// - stop(deinit=false): deactivate channel only.  Camera stays connected.
///   Next init() skips openSource — immediate activation.  Used for fast
///   algorithm switching.
/// - shutdown(port):     full teardown — deletes all sensors, deactivate,
///   removeSource, delete V4L2 source.  Called when a non-DSP device takes
///   over the port.
///
/// ## Threading
///
/// - Constructor calls DspServer::init() synchronously in the caller's thread.
/// - After successful init, DspServer is moved to mDspThread.
/// - Sensor signals → VSM slots via QueuedConnection (cross-thread safe).
/// - addSource/removeSource are BlockingQueued → caller blocks until DSP thread
///   processes.
/// - activate/deactivate are QueuedConnection → non-blocking.
///
/// ## Destruction order
///
/// destroyDsp() (quit+wait thread, reset DspServer) runs BEFORE sensor/source
/// deletion.  This guarantees no DSP signals arrive after sensors are freed.
class VideoSensorManager : public QObject
{
	Q_OBJECT
public:
	/// Constructs the manager and initialises the DSP bridge.
	///
	/// Creates DspServer, connects lifecycle signals, calls init() (blocking).
	/// If init() fails: mState is set to failed, all subsequent operations
	/// return silently.  The partially-initialised DspServer is cleaned up in
	/// ~VideoSensorManager().
	///
	/// @param configurer          parsed XML configuration (ports, devices).
	/// @param hardwareAbstraction factory for V4L2 sources and hardware files.
	explicit VideoSensorManager(const trikKernel::Configurer &configurer,
				    const trikHal::HardwareAbstractionInterface &hardwareAbstraction);

	/// Stops the DSP thread, kills LAD daemon, frees all resources.
	///
	/// Destruction order (guaranteed):
	///   1. destroyDsp() — quit thread, wait, reset DspServer
	///   2. qDeleteAll sensors (line/object/color) + clear maps
	///   3. qDeleteAll V4L2 sources + clear maps
	~VideoSensorManager() override;

	/// @name Sensor accessors
	/// These return nullptr if the sensor type was never configured for the
	/// port, or if it was destroyed (shutdown, non-DSP takeover).
	/// @{

	/// Returns the LineSensor instance for the port, or nullptr.
	LineSensorInterface *lineSensor(const QString &port);

	/// Returns the ObjectSensor instance for the port, or nullptr.
	ObjectSensorInterface *objectSensor(const QString &port);

	/// Returns the ColorSensor instance for the port, or nullptr.
	ColorSensorInterface *colorSensor(const QString &port);

	/// @}

	/// @name Port-level operations
	/// Called from Brick::configure() and Brick::stop().
	/// @{

	/// Deactivate all channels and close all sources.  Does NOT delete
	/// sensors or sources — they stay allocated for potential reuse.
	void stop();

	/// Fully release a port: delete all sensors, deactivate, remove and
	/// delete the V4L2 source.  Called when a non-DSP device takes over
	/// the port via Brick::shutdownDevice().
	///
	/// @param port  port name (e.g. "video1").
	void shutdown(const QString &port);

	/// Create or reuse a V4L2 source and sensor instance(s) for a port.
	///
	/// First call: allocates V4L2 source (not opened), creates sensor.
	/// Subsequent calls (DSP→DSP switch): reuses existing source, creates
	/// additional sensor instance — old sensors stay alive.
	///
	/// @param port         port name (e.g. "video1").
	/// @param deviceClass  "lineSensor" / "objectSensor" / "colorSensor".
	void create(const QString &port, const QString &deviceClass);

	/// @}

	/// @name Static helpers used by Brick::configure()
	/// @{

	/// Returns true if deviceClass is a DSP sensor type.
	static bool isVideoSensor(const QString &deviceClass);

	/// Returns the proxy device class used for mConfigurer ("dspSensor").
	static QString deviceClass();

	/// Port name = device name for DSP sensors (1:1 mapping).
	static QString deviceToPort(const QString &device);

	/// @}

Q_SIGNALS:
	/// @name Forwarded DspServer signals (to trikGui)
	/// @{

	/// Forwarded from DspServer::videoFrameReady.
	void videoFrameReady(const QByteArray &data,
	                     uint32_t width, uint32_t height);

	/// Forwarded from DspServer::videoDisplayStarted.
	void videoDisplayStarted();

	/// Forwarded from DspServer::videoDisplayFinished.
	void videoDisplayFinished();

	/// @}

private Q_SLOTS:
	/// Receives DSP processing results and dispatches to the correct sensor.
	/// Finds the port by source ID, then delegates to Line/Object/ColorSensor.
	void onResult(const QString &sourceId,
	              trikDsp::Algorithm algorithm,
	              trikDsp::OutArgs result);

private:
	/// Port state for a single camera.
	enum class PortStatus {
		/// Camera released or never opened.  Next init() must reopen.
		Stopped,
		/// Camera allocated (createVideoDeviceFile) but not yet registered
		/// on DspServer.  open() + addSource() needed.
		Starting,
		/// Camera open and registered on DspServer.  activate() may be
		/// called immediately without reinitialisation.
		Ready
	};

	/// Open the V4L2 source and register it with DspServer.
	/// If source is not open, calls source->open().  Then addSource().
	///
	/// @post Port status = Ready on success, Stopped on failure.
	/// @return true if the source is now open and registered.
	bool openSource(const QString &port);

	/// Remove the V4L2 source from DspServer and close it.
	/// Calls DspServer::removeSource() → disconnect + stopStreaming + close.
	/// The source object is NOT deleted (still in mSources).
	///
	/// @post Port status = Stopped.
	void closeSource(const QString &port);

	/// Activate the DSP channel for a port with the given algorithm.
	/// @param canOpen  true → may call openSource() if port is Stopped/Starting.
	///                 false → only activates if port is already Ready.
	void activateForPort(const QString &port, trikDsp::Algorithm algo,
	                     trikDsp::InArgs args, bool videoOut, bool canOpen);

	/// Handle sensor stop request.  Deactivates the channel.
	/// If deinit=true: closes the source (releases camera).
	/// If deinit=false: keeps the source on DspServer for fast re-init.
	void handleStopRequested(const QString &port, bool deinit);

	/// Factory: create sensor instance for deviceClass, connect signals.
	void createSensor(const QString &port, const QString &deviceClass);

	/// Returns true if the manager (DspServer) is in Ready state.
	bool checkManagerState(const QString &message) const;

	/// Stop DSP thread and delete DspServer.  Safe to call repeatedly.
	void destroyDsp();

	const trikKernel::Configurer &mConfigurer;
	const trikHal::HardwareAbstractionInterface &mHardwareAbstractionInterface;

	/// Manager-level state (DspServer init status).
	DeviceState mState;

	QScopedPointer<trikDsp::DspServer> mDspServer;
	QScopedPointer<QThread> mDspThread;

	/// Per-port state: Stopped / Starting / Ready.
	QHash<QString, PortStatus> mPortStatuses;

	/// V4L2 sources indexed by port.  Owned by this manager.
	QHash<QString, trikHal::VideoDeviceFileInterface*> mSources;

	/// Sensor instances indexed by port.  Owned by this manager.
	QHash<QString, LineSensor*> mLineSensors;
	QHash<QString, ColorSensor*> mColorSensors;
	QHash<QString, ObjectSensor*> mObjectSensors;
};

}

#pragma once

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QScopedPointer>
#include <QtCore/QThread>

#include "dspSource.h"
#include "dspTypes.h"
#include "trikDspDeclSpec.h"

namespace trikHal {
class VideoDeviceFileInterface;
}

namespace trikDsp {

/// ARM ↔ DSP bridge via TI IPC MessageQ over RPMsg.
///
/// Constructor starts the IPC stack (Ipc_start), performs MessageQ
/// handshake with the DSP and maps shared buffers via /dev/mem.  After
/// construction the object is moved to an internal QThread; all further
/// state changes are processed in that thread.
///
/// Sources are registered with addSource() — DspServer subscribes to
/// frameReady() signal instead of managing poll notifiers.
///
/// When a frame signal arrives, onFrameReady captures it, memcpys to the
/// shared buffer, issues a STEP command to the DSP and emits resultReady.
/// DspServer does NOT own sources; the caller is responsible for their
/// lifetime.
class TRIKDSP_EXPORT DspServer : public QObject
{
	Q_OBJECT

public:
	/// @param rprocId  remoteproc ID of the DSP core (0 for OMAP-L138).
	explicit DspServer(uint16_t rprocId, QObject *parent = nullptr);
	~DspServer() override;

	/// Register a source, start streaming and subscribe to frameReady().
	/// Does not take ownership.  Blocks the calling thread.
	bool addSource(trikHal::VideoDeviceFileInterface *source);

	/// Unsubscribe from frameReady() and stop streaming.
	/// The source itself is not deleted.
	void removeSource(trikHal::VideoDeviceFileInterface *source);

	/// Queue channel activation.  The next frame from the matching
	/// source will be processed with this algorithm and parameters.
	/// Safe from any thread (QueuedConnection).
	void activate(const DspChannel &channel);

	/// Queue deactivation — frame processing stops until activate()
	/// is called again.
	void deactivate();

signals:
	/// Emitted from the worker thread after each successfully
	/// processed frame.
	void resultReady(const QString &sourceId,
	                 Algorithm algorithm,
	                 OutArgs result);

	/// Emitted when videoOut=true after DSP processing.
	/// Carries a deep-copy of the DSP output buffer (RGB565 format),
	/// safe across thread boundaries.
	void videoFrameReady(const QByteArray &data,
	                     uint32_t width, uint32_t height);

	/// Emitted when deactivate() is called while video display was active.
	void videoDisplayFinished();

	/// Emitted when a channel with videoOut=true is activated.
	void videoDisplayStarted();

	/// Emitted if IPC initialisation fails.
	void errorOccurred(const QString &message);

private Q_SLOTS:
	void onFrameReady();

private:
	void init();

	class Impl;
	QScopedPointer<Impl> d;
	QThread mThread;
	QProcess mLadProcess;
};

}

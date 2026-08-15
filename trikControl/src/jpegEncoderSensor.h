#pragma once

#include <QtCore/QScopedPointer>

#include <trikControl/videoSensorStopFlags.h>

#include "dspSensorBase.h"

namespace trikKernel {
class Configurer;
}

namespace trikHal {
class HardwareAbstractionInterface;
class OutputDeviceFileInterface;
}

namespace trikControl {

/// DSP-based JPEG encoder. Sits alongside Line/Object/Color sensors but is NOT
/// exposed through BrickInterface: it is an internal pipeline stage that
/// consumes camera frames, asks the DSP to encode them as JPEG, and streams the
/// encoded frames (with the mjpg-streamer frame delimiter) into a FIFO for
/// input_fifo.so to pick up.
///
/// Unlike the legacy virtual-sensor workers it owns no dedicated thread: the
/// JPEG bytes are captured synchronously on the DspServer thread (they travel
/// inside trikDsp::OutArgs::jpegData) and this sensor only performs a single
/// non-blocking FIFO write from its onResult() slot.
class JpegEncoderSensor : public QObject
{
	Q_OBJECT

public:
	JpegEncoderSensor(const QString &port, const trikKernel::Configurer &configurer,
	                  const trikHal::HardwareAbstractionInterface &hardwareAbstraction);
	~JpegEncoderSensor() override;

	DeviceInterface::Status status() const;
	void onResult(trikDsp::OutArgs result);

Q_SIGNALS:
	void activateRequested(trikDsp::InArgs args, bool videoOut, bool canOpen);
	void stopRequested(int flags);
	void stopped();

public Q_SLOTS:
	void init(uint8_t jpegQuality, bool ifBlackAndWhite, bool showOnDisplay = false);
	void stop(int flags = StopAll);

private:
	void writeFrame(const QByteArray &jpegData);

	DspSensorHelper m;

	/// FIFO the encoded frames are streamed into (the write side of the
	/// mjpg-streamer input_fifo.so pipe).
	QScopedPointer<trikHal::OutputDeviceFileInterface> mFifoWriter;

	/// Frame delimiter used by mjpg-streamer's input_fifo.so to split frames.
	static const QByteArray sFrameDelimiter;
};

}

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

#include <QtCore/QObject>
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
	/// Creates the encoder and resolves the per-port output FIFO from the config.
	JpegEncoderSensor(const QString &port, const trikKernel::Configurer &configurer,
	                  const trikHal::HardwareAbstractionInterface &hardwareAbstraction);
	~JpegEncoderSensor() override;

	/// Current device state.
	DeviceInterface::Status status() const;

	/// Receives an encoded frame from the DSP and streams it into the FIFO.
	void onResult(const trikDsp::OutArgs &result);

Q_SIGNALS:
	void activateRequested(const trikDsp::InArgs &args, bool videoOut, bool canOpen);
	void stopRequested(int flags);
	void stopped();

public Q_SLOTS:
	/// Opens the FIFO and asks the DSP to start encoding frames.
	void init(uint8_t jpegQuality, bool ifBlackAndWhite, bool showOnDisplay = false);

	/// Closes the FIFO and asks the DSP to stop encoding.
	void stop(int flags = StopAll);

private:
	/// Writes one encoded frame plus the mjpg-streamer delimiter into the FIFO.
	void writeFrame(const QByteArray &jpegData);

	DspSensorHelper m;

	/// FIFO the encoded frames are streamed into (the write side of the
	/// mjpg-streamer input_fifo.so pipe).
	QScopedPointer<trikHal::OutputDeviceFileInterface> mFifoWriter;

	/// Frame delimiter used by mjpg-streamer's input_fifo.so to split frames.
	static const QByteArray sFrameDelimiter;
};

}

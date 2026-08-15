#include "jpegEncoderSensor.h"

#include <QsLog.h>

#include <trikKernel/configurer.h>
#include <trikHal/hardwareAbstractionInterface.h>
#include <trikHal/outputDeviceFileInterface.h>

using namespace trikControl;

const QByteArray JpegEncoderSensor::sFrameDelimiter = QByteArrayLiteral("c3f97bee765fd86b209951ead9f8a583");

JpegEncoderSensor::JpegEncoderSensor(const QString &port, const trikKernel::Configurer &configurer,
                                     const trikHal::HardwareAbstractionInterface &hardwareAbstraction)
	: m("Jpeg Encoder on " + port, configurer, port, trikDsp::Algorithm::Jpeg)
{
	// Each camera port streams through its own FIFO (mjpg-streamer's
	// input_fifo.so reads "/run/mjpg-encoder-<port>.out.fifo"); it may still be
	// overridden per port in the config ("outputFile").
	QString fifoName = QStringLiteral("/run/mjpg-encoder-%1.out.fifo").arg(port);
	fifoName = configurer.attributeByPort(port, "outputFile", &fifoName);

	mFifoWriter.reset(hardwareAbstraction.createOutputDeviceFile(fifoName));

	if (!m.state().isFailed()) {
		m.state().ready();
	}
}

JpegEncoderSensor::~JpegEncoderSensor()
{
	mFifoWriter->close();
	Q_EMIT stopped();
}

DeviceInterface::Status JpegEncoderSensor::status() const
{
	return m.state().status();
}

void JpegEncoderSensor::init(uint8_t jpegQuality, bool ifBlackAndWhite, bool showOnDisplay)
{
	if (!m.doInit(showOnDisplay)) {
		return;
	}

	if (!mFifoWriter->open(trikHal::OutputDeviceFileInterface::OpenMode::NonBlockingBinary)) {
		QLOG_ERROR() << "JpegEncoderSensor: failed to open output fifo" << mFifoWriter->fileName();
		m.state().fail();
		return;
	}

	m.inArgs().jpegQuality = jpegQuality;
	m.inArgs().ifBlackAndWhite = ifBlackAndWhite;

	Q_EMIT activateRequested(m.inArgs(), showOnDisplay, true);
}

void JpegEncoderSensor::stop(int flags)
{
	m.doStop();
	mFifoWriter->close();
	Q_EMIT stopRequested(flags);
	Q_EMIT stopped();
}

void JpegEncoderSensor::onResult(trikDsp::OutArgs result)
{
	if (result.jpegData.isEmpty()) {
		return;
	}

	writeFrame(result.jpegData);
}

void JpegEncoderSensor::writeFrame(const QByteArray &jpegData)
{
	// Non-blocking: drops the frame when the pipe is full (slow consumer).
	mFifoWriter->write(jpegData + sFrameDelimiter);
}

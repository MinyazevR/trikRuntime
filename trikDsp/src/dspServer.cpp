#include "dspServer.h"
#include "dspServer_p.h"
#include "dspConverters.h"

#include <QsLog.h>
#include <trikHal/VideoDeviceFileInterface.h>

namespace trikDsp {

DspServer::DspServer(uint16_t rprocId, QObject *parent)
	: QObject(parent)
	, d(new Impl)
{
	d->rprocId = rprocId;
	init();

	moveToThread(&mThread);
	mThread.setObjectName(QStringLiteral("DspServer"));
	mThread.start();
}

DspServer::~DspServer()
{
	mThread.quit();
	mThread.wait();
}

bool DspServer::addSource(trikHal::VideoDeviceFileInterface *source)
{
	bool ok = false;
	QMetaObject::invokeMethod(this, [&]() {
		if (!source->startStreaming()) {
			QLOG_ERROR() << "DspServer: startStreaming failed for" << source->id();
			return;
		}
		connect(source, &trikHal::VideoDeviceFileInterface::frameReady,
		        this, &DspServer::onFrameReady);
		ok = true;
	}, Qt::BlockingQueuedConnection);
	return ok;
}

void DspServer::removeSource(trikHal::VideoDeviceFileInterface *source)
{
	QMetaObject::invokeMethod(this, [this, source]() {
		disconnect(source, nullptr, this, nullptr);
		source->stopStreaming();
	}, Qt::BlockingQueuedConnection);
}

void DspServer::activate(const DspChannel &channel)
{
	QMetaObject::invokeMethod(this, [this, channel]() {
		d->setChannel(channel);
		if (channel.videoOut)
			emit videoDisplayStarted();
	}, Qt::QueuedConnection);
}

void DspServer::deactivate()
{
	QMetaObject::invokeMethod(this, [this]() {
		const bool wasVideoOut = d->channel().videoOut;
		d->clearChannel();
		if (wasVideoOut)
			emit videoDisplayFinished();
	}, Qt::QueuedConnection);
}

void DspServer::onFrameReady()
{
	auto *source = qobject_cast<trikHal::VideoDeviceFileInterface *>(sender());
	if (!source || source != d->channelSource())
		return;

	OutArgs out;
	VideoFrame videoFrame;
	const bool needVideo = d->channel().videoOut;
	if (d->processFrame(*source, d->channel(), out, needVideo ? &videoFrame : nullptr)) {
		emit resultReady(source->id(), d->channelAlgo(), out);
		if (needVideo && videoFrame.data) {
			const auto frameData = QByteArray(reinterpret_cast<const char *>(videoFrame.data),
			                                  static_cast<int>(videoFrame.size));
			emit videoFrameReady(frameData,
			                     videoFrame.width, videoFrame.height);
		}
	}
}

void DspServer::init()
{
	QLOG_INFO() << "DspServer: initializing";

	if (!d->startIpc()) {
		emit errorOccurred(QStringLiteral("Ipc_start failed"));
		return;
	}

	d->setupMessageQueue();
	d->mapSharedBuffers();
}

}

#include "dspServer.h"
#include "dspServer_p.h"
#include "dspConverters.h"

#include <QsLog.h>
#include <QEventLoop>
#include <QTimer>
#include <trikHal/VideoDeviceFileInterface.h>

namespace trikDsp {

DspServer::DspServer(uint16_t rprocId, QObject *parent)
	: QObject(parent)
	, d(new Impl)
{
	d->rprocId = rprocId;
}

DspServer::~DspServer()
{
	mLadProcess.terminate();
	if (!mLadProcess.waitForFinished(3000)) {
		QLOG_ERROR() << "DspServer: LAD daemon did not finish, killing";
		mLadProcess.kill();
		mLadProcess.waitForFinished(1000);
	}
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
		source->close();
	}, Qt::BlockingQueuedConnection);
}

void DspServer::activate(const DspChannel &channel)
{
	QMetaObject::invokeMethod(this, [this, channel]() {
		if (d->channel().videoOut) {
			QLOG_INFO() << "DspServer: deactivating video display for activation";
			emit videoDisplayFinished();
		}
		d->setChannel(channel);
		if (channel.videoOut) {
			QLOG_INFO() << "DspServer: activating video display for new channel";
			emit videoDisplayStarted();
		}
	}, Qt::QueuedConnection);
}

void DspServer::deactivate()
{
	QMetaObject::invokeMethod(this, [this]() {
		const bool wasVideoOut = d->channel().videoOut;
		d->clearChannel();
		if (wasVideoOut) {
			QLOG_INFO() << "DspServer: deactivating channel, was video display";
			emit videoDisplayFinished();
		}
	}, Qt::QueuedConnection);
}

void DspServer::onFrameReady()
{
	auto *source = qobject_cast<trikHal::VideoDeviceFileInterface *>(sender());
	if (!source || source != d->channelSource()) {
		QLOG_WARN() << "DspServer: dropped frame from source"
		            << (source ? source->id() : "null")
		            << "expected" << (d->channelSource() ? d->channelSource()->id() : "null");
		return;
	}

	OutArgs out;
	VideoFrame videoFrame;
	const bool needVideo = d->channel().videoOut;
	if (d->processFrame(*source, d->channel(), out, needVideo ? &videoFrame : nullptr)) {
		QLOG_INFO() << "DspServer: frame processed for source" << source->id();
		emit resultReady(source->id(), d->channelAlgo(), out);
		if (needVideo && videoFrame.data) {
			const auto frameData = QByteArray(reinterpret_cast<const char *>(videoFrame.data),
			                                  static_cast<int>(videoFrame.size));
			emit videoFrameReady(frameData,
			                     videoFrame.width, videoFrame.height);
		}
	} else {
		QLOG_WARN() << "DspServer: processFrame failed for source" << source->id();
	}
}

void DspServer::init()
{
	QLOG_INFO() << "DspServer: initializing";

	QEventLoop loop;
	bool ladOk = false;

	QTimer::singleShot(15000, &loop, [&]() {
		QLOG_ERROR() << "DspServer: timed out waiting for LAD daemon";
		loop.quit();
	});

	connect(&mLadProcess, &QProcess::started, &loop, [&]() {
		QLOG_INFO() << "DspServer: LAD daemon started";
		ladOk = true;
		loop.quit();
	});

	connect(&mLadProcess, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError error) {
		QLOG_ERROR() << "DspServer: failed to start LAD daemon:" << error;
		loop.quit();
	});

	using ExitStatus = QProcess::ExitStatus;
	connect(&mLadProcess, QOverload<int, ExitStatus>::of(&QProcess::finished), &loop,
	        [&](int exitCode, ExitStatus status) {
		if (exitCode != 0 || status != ExitStatus::NormalExit) {
			QLOG_ERROR() << "DspServer: LAD daemon exited with code"
			             << exitCode << "status" << status;
		} else {
			QLOG_INFO() << "DspServer: LAD daemon started (parent exited)";
			ladOk = true;
		}
		loop.quit();
	});

	mLadProcess.start(QStringLiteral("lad_omapl138"), QStringList(), QIODevice::ReadOnly);
	loop.exec();

	if (!ladOk) {
		QLOG_ERROR() << "DspServer: LAD daemon start failed, aborting init";
		Q_EMIT errorOccurred(QStringLiteral("LAD daemon start failed"));
		return;
	}

	if (!d->startIpc()) {
		QLOG_ERROR() << "DspServer: Ipc_start failed, aborting init";
		Q_EMIT errorOccurred(QStringLiteral("Ipc_start failed"));
		return;
	}

	if (d->setupMessageQueue() && d->mapSharedBuffers()) {
		QLOG_INFO() << "DspServer: message queue and shared buffers set up successfully";
		Q_EMIT successfullyInited();
	} else {
		QLOG_ERROR() << "DspServer: message queue or shared buffer setup failed";
	};
}

}

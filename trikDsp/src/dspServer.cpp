#include "dspServer.h"
#include "dspServer_p.h"
#include "dspConverters.h"

#include <QsLog.h>
#include <QEventLoop>
#include <QTimer>

namespace {

static const int _registerDspMetaTypes = []() {
	qRegisterMetaType<trikDsp::Algorithm>();
	qRegisterMetaType<trikDsp::InArgs>();
	qRegisterMetaType<trikDsp::OutArgs>();
	qRegisterMetaType<uint32_t>();
	return 0;
}();

} // namespace

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

void DspServer::processFrameData(const QString &sourceId, const uint8_t *data, size_t size)
{
	if (sourceId != d->channelSourceId()) {
		QLOG_WARN() << "DspServer: dropped frame from source" << sourceId
		            << "expected" << d->channelSourceId();
		return;
	}

	OutArgs out;
	const auto algo = d->channelAlgo();
	VideoFrame videoFrame;
	const bool needVideo = d->channel().videoOut;
	const auto &channel = d->channel();
	const bool ok = d->processFrame(data, size, channel, out, needVideo ? &videoFrame : nullptr);

	if (ok) {
		QLOG_INFO() << "DspServer: frame processed for source" << sourceId;
		if (needVideo && videoFrame.data) {
			const auto frameDataCopy = QByteArray(reinterpret_cast<const char *>(videoFrame.data),
			                                      static_cast<int>(videoFrame.size));
			emit videoFrameReady(frameDataCopy,
			                     videoFrame.width, videoFrame.height);
		}
	} else {
		QLOG_WARN() << "DspServer: processFrame failed for source" << sourceId;
	}

	emit resultReady(sourceId, algo, out);
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

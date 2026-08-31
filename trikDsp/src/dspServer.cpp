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

#include "dspServer.h"
#include "dspServer_p.h"
#include "dspConverters.h"

#include <QsLog.h>
#include <QEventLoop>
#include <QTimer>

#include <trikHal/fbOutputInterface.h>

#include <algorithm>
#include <cstring>

namespace {

const int _registerDspMetaTypes = []() {
	qRegisterMetaType<trikDsp::Algorithm>("trikDsp::Algorithm");
	qRegisterMetaType<trikDsp::InArgs>("trikDsp::InArgs");
	qRegisterMetaType<trikDsp::OutArgs>("trikDsp::OutArgs");
	qRegisterMetaType<uint32_t>("uint32_t");
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
	// Tear down the IPC stack while the LAD daemon is still alive: the
	// MessageQ teardown blocks on MessageQ_FOREVER and needs the daemon to
	// respond, so killing LAD first could hang forever.
	d.reset();

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
		const bool wasVideo = d->channel().videoOut && d->mFbOutput && d->mFbOutput->isOpen();
		d->setChannel(channel);
		if (channel.videoOut && d->mFbOutput) {
			if (!wasVideo) {
				QLOG_INFO() << "DspServer: opening video display via HAL fb output";
				d->mFbOutput->open();
			}
			// else: same fb, different algorithm - no reopen needed
		} else if (wasVideo) {
			QLOG_INFO() << "DspServer: closing video display via HAL fb output";
			d->mFbOutput->close();
		}
	}, Qt::QueuedConnection);
}

void DspServer::deactivate()
{
	QMetaObject::invokeMethod(this, [this]() {
		d->clearChannel();
		if (d->mFbOutput && d->mFbOutput->isOpen()) {
			QLOG_INFO() << "DspServer: deactivating video display via HAL fb output";
			d->mFbOutput->close();
		}
	}, Qt::QueuedConnection);
}

void DspServer::processFrameData(const QString &sourceId, uint32_t bufferIdx)
{
	OutArgs out{};
	const auto algo = d->channelAlgo();

	// The DSP is single-channel: frames from a non-active source are dropped.
	// They are NOT returned early, because the caller must still be notified so
	// it can release the V4L2 buffer back to the driver - otherwise the capture
	// stream stalls (notifier stays disabled until QBUF).
	if (sourceId == d->channelSourceId()) {
		VideoFrame videoFrame;
		const auto needVideo = d->channel().videoOut;
		const auto &channel = d->channel();
		// bufferIdx is the V4L2 buffer index (0..buffersPerRegion-1); map it to
		// the flat DSP input buffer index of the channel's region.
		const auto flatIdx = channel.inputBufferBase + bufferIdx;
		const auto ok = d->processFrame(channel, out, flatIdx,
		                                needVideo ? &videoFrame : nullptr);

		// autoDetect is one-shot: consume it on the DSP side so the detection
		// runs on exactly one frame (like the old runtime, which cleared the
		// command flag right after reading it). Tag the result so the consumer
		// can tell this frame apart from the others without relying on its own
		// local flag (which races with frames already in flight).
		if (channel.inArgs.autoDetect) {
			out.autoDetect = true;
			d->consumeAutoDetect();
		}

		if (ok && needVideo && videoFrame.data && d->mFbOutput && d->mFbOutput->isOpen()) {
			d->mFbOutput->writeFrame(static_cast<const uint8_t *>(videoFrame.data));
		}
	} else {
		QLOG_DEBUG() << "DspServer: dropping frame from inactive source" << sourceId
		             << "(active=" << d->channelSourceId() << ")";
	}

	// bufferIdx here is the V4L2 buffer index, which the caller needs to hand
	// the buffer back to the driver.
	emit resultReady(sourceId, algo, out, bufferIdx);
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
		Q_EMIT errorOccurred(QStringLiteral("message queue or shared buffer setup failed"));
	}
}

void DspServer::setFbOutput(trikHal::FbOutputInterface *fb)
{
	d->mFbOutput.reset(fb);
}

}

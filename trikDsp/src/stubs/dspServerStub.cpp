#include "dspServer.h"
#include "../dspServer_p.h"
#include "../dspConverters.h"

#include <QsLog.h>
#include <trikHal/VideoDeviceFileInterface.h>

namespace trikDsp {

DspServer::DspServer(uint16_t rprocId, QObject *parent)
	: QObject(parent)
	, d(new Impl)
{
	d->rprocId = rprocId;
	QLOG_INFO() << "DspServer: init skipped (stub)";
}

DspServer::~DspServer()
{
}

void DspServer::init()
{
	QLOG_INFO() << "DspServer: init (stub)";
	Q_EMIT successfullyInited();
}

bool DspServer::addSource(trikHal::VideoDeviceFileInterface *source)
{
	QLOG_INFO() << "DspServer: addSource (stub)";
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
	QLOG_INFO() << "DspServer: removeSource (stub)";
	QMetaObject::invokeMethod(this, [this, source]() {
		disconnect(source, nullptr, this, nullptr);
		source->stopStreaming();
	}, Qt::BlockingQueuedConnection);
}

void DspServer::activate(const DspChannel &channel)
{
	QLOG_INFO() << "DspServer: activate (stub)";
	QMetaObject::invokeMethod(this, [this, channel]() {
		d->setChannel(channel);
	}, Qt::QueuedConnection);
}

void DspServer::deactivate()
{
	QLOG_INFO() << "DspServer: deactivate (stub)";
	QMetaObject::invokeMethod(this, [this]() {
		d->clearChannel();
	}, Qt::QueuedConnection);
}

void DspServer::onFrameReady()
{
}

}

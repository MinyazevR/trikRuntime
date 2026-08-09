#include "dspServer.h"
#include "../dspServer_p.h"
#include "../dspConverters.h"

#include <QsLog.h>

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

void DspServer::activate(const DspChannel &channel)
{
	QLOG_INFO() << "DspServer: activate (stub)" << channel.sourceId;
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

void DspServer::processFrameData(const QString &sourceId, const uint8_t *data, size_t size)
{
	Q_UNUSED(sourceId)
	Q_UNUSED(data)
	Q_UNUSED(size)
}

}

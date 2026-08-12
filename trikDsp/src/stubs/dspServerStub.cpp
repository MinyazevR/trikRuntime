#include "dspServer.h"
#include "../dspServer_p.h"
#include "../dspConverters.h"

#include <QsLog.h>

namespace {

static const int _registerDspMetaTypes = []() {
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

void DspServer::copyFrame(const uint8_t *data, size_t size)
{
	Q_UNUSED(data)
	Q_UNUSED(size)
}

void DspServer::processFrameData(const QString &sourceId)
{
	Q_UNUSED(sourceId)
}

}

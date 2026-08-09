#include "../dspServer_p.h"
#include "dspServer.h"

#include <QsLog.h>

namespace trikDsp {

DspServer::Impl::~Impl()
{
	QLOG_INFO() << "DspServer: destroyed (stub)";
}

bool DspServer::Impl::startIpc()
{
	QLOG_INFO() << "DspServer: Ipc_start (stub)";
	return true;
}

bool DspServer::Impl::setupMessageQueue()
{
	QLOG_INFO() << "DspServer: MessageQ setup skipped (stub)";
	return true;
}

void DspServer::Impl::destroyMessageQueue()
{
	QLOG_INFO() << "DspServer: MessageQ destroy skipped (stub)";
}

bool DspServer::Impl::mapSharedBuffers()
{
	QLOG_INFO() << "DspServer: buffer mapping skipped (stub)";
	return true;
}

void DspServer::Impl::registerAlgorithm(Algorithm algo, const AlgoDescriptor &desc)
{
	Q_UNUSED(algo)
	Q_UNUSED(desc)
	QLOG_INFO() << "DspServer: algorithm registration skipped (stub)";
	mCurrentAlgo = TRIK_CV_ALGORITHM_NONE;
}

bool DspServer::Impl::step(const InArgs &in, OutArgs &out)
{
	Q_UNUSED(in)
	Q_UNUSED(out)
	return false;
}

bool DspServer::Impl::processFrame(const uint8_t *data, size_t size, const DspChannel &channel,
                                    OutArgs &out, VideoFrame *videoFrame)
{
	Q_UNUSED(data)
	Q_UNUSED(size)
	Q_UNUSED(channel)
	Q_UNUSED(out)
	Q_UNUSED(videoFrame)
	return false;
}

::trik_msg *DspServer::Impl::sendAndWaitForResponse(::trik_msg *msg)
{
	Q_UNUSED(msg)
	return nullptr;
}

void DspServer::Impl::freeMessage(::trik_msg *msg)
{
	Q_UNUSED(msg)
}

}

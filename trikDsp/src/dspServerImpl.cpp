#include "dspServer_p.h"
#include "dspConverters.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <ti/ipc/Std.h>
#include <ti/ipc/Ipc.h>
#include <ti/ipc/MessageQ.h>
#include <ti/ipc/MultiProc.h>
#include <ti/ipc/transports/TransportRpmsg.h>

#include <trik/sensors/cmd.h>
#include <trik/sensors/msg.h>

#include <QsLog.h>

namespace trikDsp {

namespace {

constexpr int PAGE_SIZE = 4096;

enum trik_cmd algoToDspCmd(enum trik_cv_algorithm algo)
{
	switch (algo) {
	case TRIK_CV_ALGORITHM_MOTION_SENSOR:
		return TRIK_CMD_MOTION_SENSOR;
	case TRIK_CV_ALGORITHM_EDGE_LINE_SENSOR:
		return TRIK_CMD_EDGE_LINE_SENSOR;
	case TRIK_CV_ALGORITHM_LINE_SENSOR:
		return TRIK_CMD_LINE_SENSOR;
	case TRIK_CV_ALGORITHM_OBJECT_SENSOR:
		return TRIK_CMD_OBJECT_SENSOR;
	case TRIK_CV_ALGORITHM_MXN_SENSOR:
		return TRIK_CMD_MXN_SENSOR;
	default:
		return TRIK_CMD_NOP;
	}
}

uint8_t *physToVirt(void *physAddr)
{
	const auto addr = reinterpret_cast<uintptr_t>(physAddr);
	const auto pageBase = addr / PAGE_SIZE * PAGE_SIZE;
	const auto pageOffset = addr - pageBase;

	const int memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (memfd < 0) {
		QLOG_ERROR() << "DspServer: open /dev/mem failed:" << strerror(errno);
		return nullptr;
	}

	auto *mapped = mmap(nullptr, pageOffset + BUFFER_SIZE,
	                    PROT_READ | PROT_WRITE, MAP_SHARED,
	                    memfd, pageBase);
	close(memfd);

	if (mapped == MAP_FAILED) {
		QLOG_ERROR() << "DspServer: mmap /dev/mem failed:" << strerror(errno);
		return nullptr;
	}

	return static_cast<uint8_t *>(mapped) + pageOffset;
}

::trik_msg *allocRequest(MessageQ_Handle hostQue, UInt16 heapId,
                              UInt32 msgSize, enum trik_cmd cmd)
{
	auto *msg = reinterpret_cast<::trik_msg *>(MessageQ_alloc(heapId, msgSize));
	if (!msg)
		return nullptr;
	msg->cmd = cmd;
	MessageQ_setReplyQueue(hostQue, reinterpret_cast<MessageQ_Msg>(msg));
	return msg;
}

}

DspServer::Impl::~Impl()
{
	destroyMessageQueue();
	Ipc_stop();

	QLOG_INFO() << "DspServer: destroyed";
}

bool DspServer::Impl::startIpc()
{
	Ipc_transportConfig(&TransportRpmsg_Factory);

	const int status = Ipc_start();
	if (status < 0) {
		QLOG_ERROR() << "DspServer: Ipc_start failed:" << status;
		return false;
	}

	return true;
}

::trik_msg *DspServer::Impl::sendAndWaitForResponse(::trik_msg *msg)
{
	if (MessageQ_put(mSlaveQue, reinterpret_cast<MessageQ_Msg>(msg)) < 0)
		return nullptr;

	::trik_msg *res = nullptr;
	MessageQ_get(mHostQue, reinterpret_cast<MessageQ_Msg *>(&res),
	             MessageQ_FOREVER);
	return res;
}

void DspServer::Impl::freeMessage(::trik_msg *msg)
{
	MessageQ_free(reinterpret_cast<MessageQ_Msg>(msg));
}

void DspServer::Impl::setupMessageQueue()
{
	MessageQ_Params params;
	MessageQ_Params_init(&params);

	mHostQue = MessageQ_create(const_cast<char *>(TRIK_HOST_MSG_QUE_NAME), &params);
	if (!mHostQue) {
		QLOG_ERROR() << "DspServer: MessageQ_create failed";
		return;
	}

	char name[32];
	sprintf(name, TRIK_SLAVE_MSG_QUE_NAME, MultiProc_getName(rprocId));

	int status = 0;
	do {
		status = MessageQ_open(name, &mSlaveQue);
		sleep(1);
	} while (status == MessageQ_E_NOTFOUND);

	if (status < 0) {
		QLOG_ERROR() << "DspServer: MessageQ_open failed:" << status;
		return;
	}

	QLOG_INFO() << "DspServer: MessageQ ready";
}

void DspServer::Impl::destroyMessageQueue()
{
	if (mSlaveQue != MessageQ_INVALIDMESSAGEQ) {
		MessageQ_close(&mSlaveQue);
		mSlaveQue = MessageQ_INVALIDMESSAGEQ;
	}
	if (mHostQue) {
		MessageQ_delete(&mHostQue);
		mHostQue = nullptr;
	}
}

void DspServer::Impl::mapSharedBuffers()
{
	auto *req = allocRequest(mHostQue, TRIK_MSG_HEAP_ID, TRIK_MSG_SIZE, TRIK_CMD_INIT);
	if (!req) {
		QLOG_ERROR() << "DspServer: failed to allocate INIT msg";
		return;
	}

	auto *res = sendAndWaitForResponse(req);
	if (!res) {
		QLOG_ERROR() << "DspServer: no response to INIT";
		return;
	}

	auto *initRes = reinterpret_cast<struct trik_res_init_msg *>(res);
	mDspIn.start = physToVirt(initRes->dsp_in_buffer);
	mDspIn.length = BUFFER_SIZE;
	mDspOut.start = physToVirt(initRes->dsp_out_buffer);
	mDspOut.length = BUFFER_SIZE;

	freeMessage(res);

	if (mDspIn.start && mDspOut.start)
		QLOG_INFO() << "DspServer: DSP buffers mapped";
	else
		QLOG_ERROR() << "DspServer: failed to mmap DSP buffers";
}

void DspServer::Impl::registerAlgorithm(Algorithm algo, const AlgoDescriptor &desc)
{
	const auto dspAlgo = toDspAlgo(algo);
	const auto cmd = algoToDspCmd(dspAlgo);
	if (cmd == TRIK_CMD_NOP) {
		QLOG_ERROR() << "DspServer: unknown algorithm";
		return;
	}

	const auto vfmt = toDspVideoFormat(desc.format);
	if (vfmt == Unknown) {
		QLOG_ERROR() << "DspServer: unknown pixel format";
		return;
	}

	auto *req = reinterpret_cast<struct trik_req_cv_algorithm_msg *>(
	    allocRequest(mHostQue, TRIK_MSG_HEAP_ID, TRIK_MSG_SIZE, cmd));
	if (!req) {
		QLOG_ERROR() << "DspServer: failed to allocate algo msg";
		return;
	}

	req->video_format = vfmt;
	req->line_length = desc.lineLength;

	auto *res = sendAndWaitForResponse(&req->header);
	if (!res) {
		QLOG_ERROR() << "DspServer: no ack for algo reg";
		return;
	}

	freeMessage(res);
}

bool DspServer::Impl::step(const InArgs &in, OutArgs &out)
{
	const auto dspIn = toDspInArgs(in);

	auto *req = reinterpret_cast<struct trik_res_step_msg *>(
	    allocRequest(mHostQue, TRIK_MSG_HEAP_ID, TRIK_MSG_SIZE, TRIK_CMD_STEP));
	if (!req)
		return false;

	req->in_args = dspIn;

	auto *res = sendAndWaitForResponse(&req->header);
	if (!res)
		return false;

	auto *stepRes = reinterpret_cast<struct trik_res_step_msg *>(res);
	out = fromDspOutArgs(stepRes->out_args);

	freeMessage(res);
	return true;
}

bool DspServer::Impl::processFrame(trikHal::VideoDeviceFileInterface &source, const DspChannel &channel,
                                   OutArgs &out, VideoFrame *videoFrame)
{
	const uint8_t *data = nullptr;
	size_t size = 0;

	if (!source.capture(data, size))
		return false;

	const auto dspAlgo = toDspAlgo(channel.algorithm);
	if (dspAlgo != mCurrentAlgo) {
		AlgoDescriptor desc = {fromV4l2Fourcc(source.actualFourcc()), source.bytesPerLine()};
		registerAlgorithm(channel.algorithm, desc);
		mCurrentAlgo = dspAlgo;
	}

	memcpy(mDspIn.start, data, std::min(size, mDspIn.length));

	const bool ok = step(channel.inArgs, out);

	if (channel.videoOut) {
		videoFrame->data = static_cast<const uint8_t *>(mDspOut.start);
		videoFrame->size = mDspOut.length;
		videoFrame->width = source.actualWidth();
		videoFrame->height = source.actualHeight();
	}

	source.release();
	return ok;
}

} // namespace trikDsp

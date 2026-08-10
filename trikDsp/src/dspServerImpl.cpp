#include "dspServer_p.h"
#include "dspConverters.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
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
constexpr int MSG_QUEUE_RETRIES = 10;

constexpr int DSP_IMG_WIDTH = 320;
constexpr int DSP_IMG_HEIGHT = 240;

enum trik_cmd algoToDspCmd(enum trik_cv_algorithm algo)
{
	switch (algo) {
	case TRIK_CV_ALGORITHM_MOTION_SENSOR:   return TRIK_CMD_MOTION_SENSOR;
	case TRIK_CV_ALGORITHM_EDGE_LINE_SENSOR: return TRIK_CMD_EDGE_LINE_SENSOR;
	case TRIK_CV_ALGORITHM_LINE_SENSOR:     return TRIK_CMD_LINE_SENSOR;
	case TRIK_CV_ALGORITHM_OBJECT_SENSOR:   return TRIK_CMD_OBJECT_SENSOR;
	case TRIK_CV_ALGORITHM_MXN_SENSOR:      return TRIK_CMD_MXN_SENSOR;
	default:                                 return TRIK_CMD_NOP;
	}
}

struct MmapResult {
	uint8_t *data = nullptr;
	void *base = nullptr;
	size_t len = 0;
};

MmapResult physToVirt(void *physAddr)
{
	const auto addr = reinterpret_cast<uintptr_t>(physAddr);
	const auto pageBase = addr / PAGE_SIZE * PAGE_SIZE;
	const auto pageOffset = addr - pageBase;

	const int memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (memfd < 0) {
		QLOG_ERROR() << "DspServer: open /dev/mem failed:" << strerror(errno);
		return {};
	}

	const size_t mapLen = pageOffset + BUFFER_SIZE;
	auto *mapped = mmap(nullptr, mapLen,
	                    PROT_READ | PROT_WRITE, MAP_SHARED,
	                    memfd, pageBase);
	close(memfd);

	if (mapped == MAP_FAILED) {
		QLOG_ERROR() << "DspServer: mmap /dev/mem failed:" << strerror(errno);
		return {};
	}

	return {static_cast<uint8_t*>(mapped) + pageOffset, mapped, mapLen};
}

::trik_msg *allocRequest(MessageQ_Handle hostQue, UInt16 heapId,
                         UInt32 msgSize, enum trik_cmd cmd)
{
	auto *msg = reinterpret_cast<::trik_msg *>(MessageQ_alloc(heapId, msgSize));
	if (!msg) {
		QLOG_ERROR() << "DspServer: MessageQ_alloc failed (heap" << heapId
		             << "size" << msgSize << "cmd" << cmd << ")";
		return nullptr;
	}
	msg->cmd = cmd;
	MessageQ_setReplyQueue(hostQue, reinterpret_cast<MessageQ_Msg>(msg));
	return msg;
}

} // namespace

DspServer::Impl::~Impl()
{
	destroyMessageQueue();
	Ipc_stop();

	if (mMmapIn) {
		QLOG_INFO() << "DspServer: unmapping input buffer";
		munmap(mMmapIn, mMmapInLen);
	}
	if (mMmapOut) {
		QLOG_INFO() << "DspServer: unmapping output buffer";
		munmap(mMmapOut, mMmapOutLen);
	}

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
	if (MessageQ_put(mSlaveQue, reinterpret_cast<MessageQ_Msg>(msg)) < 0) {
		QLOG_ERROR() << "DspServer: MessageQ_put failed for cmd" << msg->cmd;
		return nullptr;
	}

	::trik_msg *res = nullptr;
	MessageQ_get(mHostQue, reinterpret_cast<MessageQ_Msg *>(&res),
	             MessageQ_FOREVER);
	return res;
}

void DspServer::Impl::freeMessage(::trik_msg *msg)
{
	MessageQ_free(reinterpret_cast<MessageQ_Msg>(msg));
}

bool DspServer::Impl::setupMessageQueue()
{
	MessageQ_Params params;
	MessageQ_Params_init(&params);

	mHostQue = MessageQ_create(const_cast<char *>(TRIK_HOST_MSG_QUE_NAME), &params);
	if (!mHostQue) {
		QLOG_ERROR() << "DspServer: MessageQ_create failed";
		return false;
	}

	std::array<char, 32> name;
	snprintf(name.data(), name.size(),
	         TRIK_SLAVE_MSG_QUE_NAME, MultiProc_getName(rprocId));

	int status = 0;
	for (int retry = 0; retry < MSG_QUEUE_RETRIES; ++retry) {
		status = MessageQ_open(name.data(), &mSlaveQue);
		if (status != MessageQ_E_NOTFOUND)
			break;
		sleep(1);
	}

	if (status == MessageQ_E_NOTFOUND) {
		QLOG_ERROR() << "DspServer: MessageQ_open timed out after"
		             << MSG_QUEUE_RETRIES << "retries";
		return false;
	}

	if (status < 0) {
		QLOG_ERROR() << "DspServer: MessageQ_open failed:" << status;
		return false;
	}

	QLOG_INFO() << "DspServer: MessageQ ready";
	return true;
}

void DspServer::Impl::destroyMessageQueue()
{
	if (mSlaveQue != MessageQ_INVALIDMESSAGEQ) {
		QLOG_INFO() << "DspServer: closing slave message queue";
		MessageQ_close(&mSlaveQue);
		mSlaveQue = MessageQ_INVALIDMESSAGEQ;
	}
	if (mHostQue) {
		QLOG_INFO() << "DspServer: deleting host message queue";
		MessageQ_delete(&mHostQue);
		mHostQue = nullptr;
	}
}

bool DspServer::Impl::mapSharedBuffers()
{
	auto *req = allocRequest(mHostQue, TRIK_MSG_HEAP_ID, TRIK_MSG_SIZE, TRIK_CMD_INIT);
	if (!req) {
		QLOG_ERROR() << "DspServer: failed to allocate INIT msg";
		return false;
	}

	auto *res = sendAndWaitForResponse(req);
	if (!res) {
		QLOG_ERROR() << "DspServer: no response to INIT";
		return false;
	}

	auto *initRes = reinterpret_cast<struct trik_res_init_msg *>(res);
	auto inMap = physToVirt(initRes->dsp_in_buffer);
	auto outMap = physToVirt(initRes->dsp_out_buffer);

	if (inMap.data) {
		QLOG_INFO() << "DspServer: mapped DSP input buffer at" << inMap.data;
		mDspIn.start = inMap.data;
		mDspIn.length = BUFFER_SIZE;
		mMmapIn = inMap.base;
		mMmapInLen = inMap.len;
	}
	if (outMap.data) {
		QLOG_INFO() << "DspServer: mapped DSP output buffer at" << outMap.data;
		mDspOut.start = outMap.data;
		mDspOut.length = BUFFER_SIZE;
		mMmapOut = outMap.base;
		mMmapOutLen = outMap.len;
	}

	freeMessage(res);

	if (mDspIn.start && mDspOut.start) {
		QLOG_INFO() << "DspServer: DSP buffers mapped";
		return true;
	}

	QLOG_ERROR() << "DspServer: failed to mmap DSP buffers";
	return false;
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
	auto *req = reinterpret_cast<struct trik_res_step_msg *>(
	    allocRequest(mHostQue, TRIK_MSG_HEAP_ID, TRIK_MSG_SIZE, TRIK_CMD_STEP));
	if (!req) {
		QLOG_ERROR() << "DspServer: failed to allocate STEP msg";
		return false;
	}

	req->in_args = toDspInArgs(in);

	auto *res = sendAndWaitForResponse(&req->header);
	if (!res) {
		QLOG_ERROR() << "DspServer: no response to STEP";
		return false;
	}

	out = fromDspOutArgs(reinterpret_cast<struct trik_res_step_msg *>(res)->out_args);

	freeMessage(res);
	return true;
}

bool DspServer::Impl::processFrame(const uint8_t *data, size_t size, const DspChannel &channel,
                                    OutArgs &out, VideoFrame *videoFrame)
{
	const auto dspAlgo = toDspAlgo(channel.algorithm);
	if (dspAlgo != mCurrentAlgo) {
		QLOG_INFO() << "DspServer: switching algorithm from" << mCurrentAlgo
		            << "to" << dspAlgo;
		AlgoDescriptor desc = {trikKernel::fromV4l2Fourcc(V4L2_PIX_FMT_YUYV),
		                       static_cast<uint32_t>(channel.width * 2)};
		registerAlgorithm(channel.algorithm, desc);
		mCurrentAlgo = dspAlgo;
	}

	memcpy(mDspIn.start, data, std::min(size, mDspIn.length));

	{
		const auto *p = static_cast<const uint8_t *>(data);
		QLOG_DEBUG() << "DspServer: V4L2 input dump [0-31]:"
		             << Qt::hex << p[0] << p[1] << p[2] << p[3]
		             << p[4] << p[5] << p[6] << p[7]
		             << p[8] << p[9] << p[10] << p[11]
		             << p[12] << p[13] << p[14] << p[15]
		             << p[16] << p[17] << p[18] << p[19]
		             << p[20] << p[21] << p[22] << p[23]
		             << p[24] << p[25] << p[26] << p[27]
		             << p[28] << p[29] << p[30] << p[31];
	}

	const bool ok = step(channel.inArgs, out);

	if (channel.videoOut) {
		const auto *p = static_cast<const uint8_t *>(mDspOut.start);
		QLOG_DEBUG() << "DspServer: DSP output dump [0-31]:"
		             << Qt::hex << p[0] << p[1] << p[2] << p[3]
		             << p[4] << p[5] << p[6] << p[7]
		             << p[8] << p[9] << p[10] << p[11]
		             << p[12] << p[13] << p[14] << p[15]
		             << p[16] << p[17] << p[18] << p[19]
		             << p[20] << p[21] << p[22] << p[23]
		             << p[24] << p[25] << p[26] << p[27]
		             << p[28] << p[29] << p[30] << p[31];
		QLOG_INFO() << "DspServer: filling video frame from DSP output buffer"
		            << channel.sourceId;
		videoFrame->data = static_cast<const uint8_t *>(mDspOut.start);
		videoFrame->size = BUFFER_SIZE_FOR_FB;
		videoFrame->width = DSP_IMG_HEIGHT;
		videoFrame->height = DSP_IMG_HEIGHT;
	}

	return ok;
}

} // namespace trikDsp


#pragma once

#include <trik/buffer.h>
#include <trik/sensors/cv_algorithm.h>
#include <trik/sensors/cv_algorithm_args.h>

#include "dspServer.h"

struct MessageQ_Object;
struct trik_msg;

namespace trikDsp {

class DspServer::Impl
{
public:
	~Impl();

	bool startIpc();
	void setupMessageQueue();
	void destroyMessageQueue();
	void mapSharedBuffers();

	void registerAlgorithm(Algorithm algo, const AlgoDescriptor &desc);
	bool step(const InArgs &in, OutArgs &out);

	bool processFrame(trikHal::VideoDeviceFileInterface &source, const DspChannel &channel,
	                  OutArgs &out, VideoFrame *videoFrame = nullptr);

	void setChannel(const DspChannel &c) { mActive = c; }
	void clearChannel() { mActive = {}; }

	const DspChannel &channel() const { return mActive; }
	Algorithm channelAlgo() const { return mActive.algorithm; }
	trikHal::VideoDeviceFileInterface *channelSource() const { return mActive.source; }

	uint16_t rprocId = 0;

private:
	::trik_msg *sendAndWaitForResponse(::trik_msg *msg);
	static void freeMessage(::trik_msg *msg);

	DspChannel mActive;
	enum trik_cv_algorithm mCurrentAlgo = TRIK_CV_ALGORITHM_NONE;

	struct MessageQ_Object *mHostQue = nullptr;
	unsigned mSlaveQue = 0xffff;
	struct buffer mDspIn = {};
	struct buffer mDspOut = {};
};

}

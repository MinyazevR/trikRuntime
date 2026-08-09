#pragma once

#include <QtCore/QString>

#include "dspTypes.h"

namespace trikDsp {

/// Format parameters sent to the DSP during algorithm registration.
struct AlgoDescriptor {
	PixelFormat format = PixelFormat::Unknown;
	uint32_t lineLength = 0;
};

struct DspChannel {
	QString sourceId;
	Algorithm algorithm = Algorithm::None;
	InArgs inArgs = {};
	bool videoOut = false;
	uint32_t width = 0;
	uint32_t height = 0;
};

struct VideoFrame {
	const uint8_t *data = nullptr;
	size_t size = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

}

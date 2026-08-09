#pragma once

#include <QtCore/QString>
#include <QMetaType>
#include <cstdint>
#include <trikKernel/videoUtils.h>

namespace trikDsp {

/// DSP CV algorithm identifiers.  Mirrors the C674x firmware capabilities.
enum class Algorithm {
	None = -1,
	Motion,
	EdgeLine,
	Line,
	Object,
	Mxn
};

using trikKernel::PixelFormat;
using trikKernel::pixelFormatFromString;
using trikKernel::toV4l2Fourcc;
using trikKernel::fromV4l2Fourcc;

/// Detected target coordinates returned by the DSP.
struct Location {
	int16_t x = 0;
	int16_t y = 0;
	uint16_t size = 0;
};

struct HsvRange {
	uint8_t from = 0;
	uint8_t to = 0;
};

/// HSV detection window — all three channels (H, S, V).
struct DetectParams {
	HsvRange hue;
	HsvRange saturation;
	HsvRange value;
};

/// Arguments sent to the DSP with each frame.
struct InArgs {
	DetectParams params;
	bool autoDetect = false;    ///< set by detect(), cleared on first result
	uint8_t m = 0;              ///< MxN grid columns (MxnSensor only)
	uint8_t n = 0;              ///< MxN grid rows    (MxnSensor only)
};

/// Result returned by the DSP for one processed frame.
struct OutArgs {
	Location location;          ///< target position (Line / Object)
	DetectParams detected;      ///< auto-detected HSV (non-zero when autoDetect was set)
	uint32_t colors[9] = {};    ///< MxN colour grid (MxnSensor only)
};

} // namespace trikDsp

Q_DECLARE_METATYPE(trikDsp::InArgs)

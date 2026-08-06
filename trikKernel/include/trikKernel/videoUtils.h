/* Copyright 2024 CyberTech Labs Ltd.
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

#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>
#include <linux/videodev2.h>

#include "trikKernelDeclSpec.h"

namespace trikKernel {

enum class PixelFormat {
	Nv16,
	Yuyv,
	Unknown
};

inline PixelFormat pixelFormatFromString(const QString &s)
{
	if (s == QStringLiteral("nv16"))
		return PixelFormat::Nv16;
	if (s == QStringLiteral("yuyv"))
		return PixelFormat::Yuyv;
	return PixelFormat::Unknown;
}

inline uint32_t toV4l2Fourcc(PixelFormat fmt)
{
	switch (fmt) {
	case PixelFormat::Nv16: return V4L2_PIX_FMT_NV16;
	case PixelFormat::Yuyv: return V4L2_PIX_FMT_YUYV;
	default:                return V4L2_PIX_FMT_YUYV;
	}
}

inline PixelFormat fromV4l2Fourcc(uint32_t fourcc)
{
	if (fourcc == V4L2_PIX_FMT_NV16) return PixelFormat::Nv16;
	if (fourcc == V4L2_PIX_FMT_YUYV) return PixelFormat::Yuyv;
	return PixelFormat::Unknown;
}

class TRIKKERNEL_EXPORT VideoUtils
{
public:
	static QVector<uint8_t> yuyvToRgb(const QVector<uint8_t> &shot, int height, int width);
	static QVector<uint8_t> yuv422pToRgb(const QVector<uint8_t> &shot, int height, int width);
};

} // namespace trikKernel

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

#include "videoUtils.h"

namespace {

inline unsigned char clip255(int x)
{
	return x >= 255 ? 255 : (x <= 0 ? 0 : static_cast<uint8_t>(x));
}

}

namespace trikKernel {

QVector<uint8_t> VideoUtils::yuyvToRgb(const QVector<uint8_t> &shot, int height, int width)
{
	QVector<uint8_t> result(height * width * 3);
	int startIndex = 0;
	for (auto row = 0; row < height; ++row) {
		for (auto col = 0; col < width; col += 2) {
			auto y0 = shot[startIndex];
			auto cb = shot[startIndex + 1];
			auto y1 = shot[startIndex + 2];
			auto cr = shot[startIndex + 3];
			startIndex += 4;

			auto resRgb = &result[(row * width + col) * 3];
			const auto alpha = 180 * (cr - 128) / 128;
			const auto beta  = 45 * (cb - 128) / 128;
			resRgb[0] = clip255(y0 + alpha);
			resRgb[1] = clip255(y0 - beta - alpha / 2);
			resRgb[2] = clip255(y0 + 5 * beta);
			resRgb[3] = clip255(y1 + alpha);
			resRgb[4] = clip255(y1 - beta - alpha / 2);
			resRgb[5] = clip255(y1 + 5 * beta);
		}
	}
	return result;
}

QVector<uint8_t> VideoUtils::yuv422pToRgb(const QVector<uint8_t> &shot, int height, int width)
{
	QVector<uint8_t> result(height * width * 3);
	if (width <= 0 || height <= 0)
		return result;

	const auto Y  = &shot[0];
	const auto UV = &shot[width * height];

	for (auto row = 0u; row < static_cast<uint32_t>(height); ++row) {
		for (auto col = 0; col < width; col += 2) {
			auto startIndex = row * width + col;
			int const y1 = Y[startIndex] - 16;
			int const y2 = Y[startIndex + 1] - 16;
			int const u  = UV[startIndex] - 128;
			int const v  = UV[startIndex + 1] - 128;
			auto _298y1 = 298 * y1;
			auto _298y2 = 298 * y2;
			auto _409v  = 409 * v;
			auto _100u  = -100 * u;
			auto _516u  = 516 * u;
			auto _208v  = -208 * v;
			auto r1 = clip255((_298y1 + _516u + 128) >> 8);
			auto g1 = clip255((_298y1 + _100u + _208v + 128) >> 8);
			auto b1 = clip255((_298y1 + _409v + 128) >> 8);
			auto r2 = clip255((_298y2 + _516u + 128) >> 8);
			auto g2 = clip255((_298y2 + _100u + _208v + 128) >> 8);
			auto b2 = clip255((_298y2 + _409v + 128) >> 8);

			auto rgb = &result[startIndex * 3];
			rgb[0] = r1; rgb[1] = g1; rgb[2] = b1;
			rgb[3] = r2; rgb[4] = g2; rgb[5] = b2;
		}
	}
	return result;
}

} // namespace trikKernel

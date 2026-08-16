/* Copyright 2018 Ivan Tyulyandin and CyberTech Labs Ltd.
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

#include "v4l2CameraImplementation.h"
#include "cameraManager.h"

#include <QtCore/QEventLoop>
#include <QtCore/QSharedPointer>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtGui/QImage>
#include <QsLog.h>
#include <linux/videodev2.h>

using namespace trikControl;

namespace {

inline unsigned char clip255(int x)
{
	return x >= 255 ? 255 : (x <= 0 ? 0 : static_cast<uint8_t>(x));
}

QVector<uint8_t> yuyvToRgb(const QVector<uint8_t> &shot, int height, int width)
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

QVector<uint8_t> yuv422pToRgb(const QVector<uint8_t> &shot, int height, int width)
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

}

V4l2CameraImplementation::V4l2CameraImplementation(const QString &port, CameraManager &cameraManager) // NOLINT(modernize-pass-by-value)
	: mPort(port)
	, mCameraManager(cameraManager)
{
}

QVector<uint8_t> V4l2CameraImplementation::getPhoto()
{
	// acquire() is asynchronous (the CameraManager lives on its own thread), so
	// wait here for its completion signal. This method runs in a dedicated
	// thread (CameraDevice::getPhoto), so a local event loop is fine.
	QEventLoop acquireLoop;
	bool acquired = false;
	const QMetaObject::Connection acquireConn = QObject::connect(&mCameraManager,
		&CameraManager::acquired, &acquireLoop,
		[this, &acquired, &acquireLoop](const QString &port, bool ok) {
			if (port == mPort) {
				acquired = ok;
				acquireLoop.quit();
			}
		});
	mCameraManager.acquire(mPort);
	acquireLoop.exec();
	QObject::disconnect(acquireConn);

	if (!acquired)
		return {};

	// subscribeLatest() below makes the CameraManager (re)start the stream for
	// us if a video sensor had parked it (StopStream), so no explicit
	// startStreaming() is needed here.

	// The device is not touched here - only the port's static format from the
	// CameraManager is used to decode the frame.
	const uint32_t fourcc = mCameraManager.fourcc(mPort);
	const int width = static_cast<int>(mCameraManager.width(mPort));
	const int height = static_cast<int>(mCameraManager.height(mPort));

	// Wait for the first latched frame. While a pull client is registered the
	// CameraManager latches every frame into a private copy and notifies via
	// latestFrameReady(); the latched buffer is ref-counted, so the raw V4L2
	// mmap buffer is never held by this thread.
	//
	// A watchdog bounds the wait. Our acquire() keeps the refcount >= 1, so a
	// polite StopAll/release() cannot close the device underneath us - but
	// Brick::stop() -> CameraManager::stop() (tearDownLocked) force-closes it
	// regardless of refcount, and a stuck camera also never latches a frame. In
	// both cases latestFrameReady() never fires and, without the timeout, this
	// loop would block forever.
	QEventLoop loop;
	QTimer watchdog;
	watchdog.setInterval(5000);
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);

	const QMetaObject::Connection conn = QObject::connect(&mCameraManager,
		&CameraManager::latestFrameReady, &loop,
		[this, &loop](const QString &port) {
			if (port == mPort)
				loop.quit();
		});

	mCameraManager.subscribeLatest(mPort, &loop);
	loop.exec();

	watchdog.stop();
	QObject::disconnect(conn);
	const QSharedPointer<QByteArray> frame = mCameraManager.grabLatestFrame(mPort);
	mCameraManager.unsubscribeLatest(mPort, &loop);
	mCameraManager.release(mPort);

	if (frame.isNull() || frame->isEmpty())
		return {};

	const QVector<uint8_t> raw(frame->cbegin(), frame->cend());
	QVector<uint8_t> rgb;
	switch (fourcc) {
	case V4L2_PIX_FMT_NV16:
		rgb = yuv422pToRgb(raw, height, width);
		break;
	case V4L2_PIX_FMT_YUYV:
		rgb = yuyvToRgb(raw, height, width);
		break;
	default:
		break;
	}

	if (rgb.isEmpty())
		return {};

	const QImage image(rgb.data(), width, height, QImage::Format_RGB888);
	image.save(getTempDir().filePath("photo.jpg"), "JPG");
	return rgb;
}

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

#include <trikKernel/videoUtils.h>

#include <QtCore/QEventLoop>
#include <QtCore/QSharedPointer>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QsLog.h>
#include <linux/videodev2.h>

using namespace trikControl;

V4l2CameraImplementation::V4l2CameraImplementation(const QString &port, CameraManager &cameraManager)
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
		rgb = trikKernel::VideoUtils::yuv422pToRgb(raw, height, width);
		break;
	case V4L2_PIX_FMT_YUYV:
		rgb = trikKernel::VideoUtils::yuyvToRgb(raw, height, width);
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

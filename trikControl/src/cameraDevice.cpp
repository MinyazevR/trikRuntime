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

#include "cameraDevice.h"

#include "qtCameraImplementation.h"
#include "v4l2CameraImplementation.h"
#include "imitationCameraImplementation.h"
#include "cameraManager.h"
#include <QsLog.h>
#include <trikKernel/configurer.h>
#include "configurerHelper.h"
#include <QEventLoop>
#include <QThread>
#include <functional>

namespace trikControl {

CameraDevice::CameraDevice(const QString & port, const QString & mediaPath,
                           const trikKernel::Configurer &configurer,
                           CameraManager &cameraManager)
{
	QString type = configurer.childAttributeByPort(port, "photo", "type");
	// The device file is owned by the CameraManager, keyed by port — no need
	// to read it from the config again.
	const QString src = cameraManager.deviceFile(port);

	if (type == "qtmultimedia") {
		decltype(mCameraImpl)(new QtCameraImplementation(src)).swap(mCameraImpl);
	} else if (type == "v4l2") {
#ifdef Q_OS_LINUX
		decltype(mCameraImpl)(new V4l2CameraImplementation(port, cameraManager)).swap(mCameraImpl);
#endif
	} else if (type == "file") {
		QStringList filters = configurer.attributeByPort(port, "filters").split(',');
		decltype(mCameraImpl)(new ImitationCameraImplementation(filters, mediaPath)).swap(mCameraImpl);
	}

	if (mCameraImpl) {
		mCameraImpl->setTempDir(mediaPath);
	} else {
		QLOG_ERROR() << "Failed to initialize camera device for " << src
		             << ", falling back to imitation";
		decltype(mCameraImpl)(new ImitationCameraImplementation(QStringList({"*.jpg","*.png"}), mediaPath))
			.swap(mCameraImpl);
		mCameraImpl->setTempDir(mediaPath);
	}
}

QVector<uint8_t> CameraDevice::getPhoto() {
	if (!mCameraImpl) return {};
	QMutexLocker lock(&mCameraMutex);

	QVector<uint8_t> photo;
	std::function<void()> runFunc = [this, &photo]{ mCameraImpl->getPhoto().swap(photo); };
#if QT_VERSION_MAJOR>=5 && QT_VERSION_MINOR>=10
	QScopedPointer<QThread> t(QThread::create(std::move(runFunc)));
#else
	struct CameraThread: public QThread {
		explicit CameraThread(std::function<void()> &&f): mF(f) {}
		void run() override { mF(); }
		std::function<void()> mF;
	};
	QScopedPointer<QThread> t(new CameraThread(std::move(runFunc)));
#endif
	QEventLoop l;
	QObject::connect(t.data(), &QThread::finished, &l, &QEventLoop::quit);
	t->setObjectName("CameraDevice::getPhoto");
	t->start();
	l.exec();
	return photo;
}

CameraDevice::Status CameraDevice::status() const { return CameraDevice::Status::ready; }

}

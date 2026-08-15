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

#include <VideoDeviceFileInterface.h>

#include <QtCore/QScopedPointer>
#include <QtCore/QVector>
#include <cstdint>
#include <cstddef>

class QSocketNotifier;

namespace trikHal {

class VideoDeviceFileBase : public VideoDeviceFileInterface
{
	Q_OBJECT

public:
	VideoDeviceFileBase(const QString &devicePath,
	                    uint32_t width, uint32_t height,
	                    uint32_t preferredFourcc,
	                    uint32_t bufferCount = 3,
	                    bool isWebcam = false,
	                    QObject *parent = nullptr);
	~VideoDeviceFileBase() override;

	bool open() override;
	bool startStreaming(bool forDsp = false) override;
	void stopStreaming() override;
	void close() override;
	bool capture(const uint8_t *&data, size_t &size) override;
	void release() override;
	bool isOpen() const override;

	uint32_t actualWidth() const override { return mWidth; }
	uint32_t actualHeight() const override { return mHeight; }
	uint32_t actualFourcc() const override { return mActualFourcc; }
	uint32_t bytesPerLine() const override { return mLineLen; }

	QString id() const override { return mPath; }

protected:
	virtual bool setFormat();
	virtual void onFrameReady(const uint8_t *data, size_t size);

	/// Apply the default V4L2 control values for a USB (UVC) webcam. Mirrors the
	/// init_webcam() step previously performed by the media-sensor init script:
	/// anti-flicker, fixed white balance and gain. Exposure is left on auto.
	void applyWebcamDefaults();

	/// Lock the exposure to manual after the auto-exposure has stabilized while
	/// streaming. Mirrors the fix_webcam() (exposure_auto=1) step, which ran
	/// after a 1s stabilization sleep in the init script. Non-blocking: the lock
	/// is applied by a single-shot timer so startStreaming() returns immediately.
	void fixWebcamExposure();

	/// Set a single V4L2 control on the device.
	bool setControl(uint32_t id, int32_t value);

	virtual bool allocateBuffers();
	virtual void freeBuffers();

	QString mPath;
	uint32_t mWidth;
	uint32_t mHeight;
	uint32_t mRequestedFourcc;
	uint32_t mActualFourcc = 0;
	uint32_t mLineLen = 0;
	uint32_t mBufferCount;
	bool mIsWebcam = false;
	bool mExposureFixed = false;
	int mFd = -1;
	bool mStreaming = false;

	struct MmapBuf {
		uint8_t *data = nullptr;
		size_t size = 0;
	};
	QVector<MmapBuf> mMmapBufs;

	const uint8_t *mCurrentData = nullptr;
	size_t mCurrentSize = 0;
	int mCurrentBufIdx = -1;

private Q_SLOTS:
	void onActivated(int fd);

private:
	QScopedPointer<QSocketNotifier> mNotifier;
};

} // namespace trikHal

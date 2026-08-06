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

#include "trikV4l2VideoDevice.h"

#include <cerrno>
#include <cstring>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <QsLog.h>

TrikV4l2VideoDevice::TrikV4l2VideoDevice(const QString &inputFile)
	: trikHal::VideoDeviceFileBase(inputFile, 320, 240, V4L2_PIX_FMT_NV16, 1)
{
}

bool TrikV4l2VideoDevice::negotiateFormat()
{
	__u32 fmtIdx = 0;
	char descPixelFmt[32] = {};
	bool found = false;

	do {
		v4l2_fmtdesc fmtTry = {};
		fmtTry.index = fmtIdx;
		fmtTry.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (ioctl(mFd, VIDIOC_ENUM_FMT, &fmtTry) == 0) {
			QLOG_INFO() << "V4l2: available format:" << hex << fmtTry.pixelformat;
			memcpy(descPixelFmt, fmtTry.description, sizeof(descPixelFmt));

			if (fmtTry.pixelformat == V4L2_PIX_FMT_NV16) {
				QLOG_INFO() << "V4l2: found format V4L2_PIX_FMT_NV16 (NV16)";
				mActualFourcc = V4L2_PIX_FMT_NV16;
				found = true;
				break;
			}
			if (fmtTry.pixelformat == V4L2_PIX_FMT_YUYV) {
				QLOG_INFO() << "V4l2: found format V4L2_PIX_FMT_YUYV (YUV422)";
				mActualFourcc = V4L2_PIX_FMT_YUYV;
				found = true;
				break;
			}
		}
		++fmtIdx;
	} while (errno != EINVAL);

	if (!found) {
		QLOG_ERROR() << "TRIK Runtime cannot convert" << descPixelFmt
		             << "to RGB888, getPhoto will return empty vector";
		return false;
	}

	QLOG_INFO() << "V4l2: selected format" << descPixelFmt;

	v4l2_std_id stdid = V4L2_STD_625_50;
	if (ioctl(mFd, VIDIOC_S_STD, &stdid) < 0) {
		if (ioctl(mFd, VIDIOC_G_STD, &stdid) == 0) {
			QLOG_INFO() << "VIDIOC_G_STD returned" << QString("%1").arg(stdid, 0, 16);
		}
	}

	return true;
}

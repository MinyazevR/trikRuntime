#include "videoDisplayProvider.h"

#include <trikControl/brickInterface.h>
#include <QsLog.h>

namespace trikGui {

VideoDisplayProvider::VideoDisplayProvider(trikControl::BrickInterface &brick)
	: QQuickImageProvider(QQuickImageProvider::Image)
{
	connect(&brick, &trikControl::BrickInterface::videoFrameReady,
	        this, &VideoDisplayProvider::updateFrame,
	        Qt::QueuedConnection);
	connect(&brick, &trikControl::BrickInterface::videoDisplayStarted,
	        this, &VideoDisplayProvider::displayStarted,
	        Qt::QueuedConnection);
	connect(&brick, &trikControl::BrickInterface::videoDisplayFinished,
	        this, &VideoDisplayProvider::displayFinished,
	        Qt::QueuedConnection);
}

QImage VideoDisplayProvider::requestImage(const QString &id, QSize *size,
					   const QSize &requestedSize)
{
	Q_UNUSED(requestedSize)

	QLOG_DEBUG() << "VideoDisplayProvider: requestImage id" << id
	             << "size" << (size ? mFrame.size() : QSize());
	if (size) {
		*size = mFrame.size();
	}
	return mFrame;
}

void VideoDisplayProvider::updateFrame(QByteArray data, uint32_t width, uint32_t height)
{
	if (data.isEmpty() || width == 0 || height == 0) {
		QLOG_WARN() << "VideoDisplayProvider: updateFrame skipped — empty data or zero dimensions"
		            << "dataSize" << data.size() << "width" << width << "height" << height;
		return;
	}

	auto *buf = new QByteArray(std::move(data));
	mFrame = QImage(reinterpret_cast<const uchar *>(buf->constData()),
			static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB16,
			[](void *p) { delete static_cast<QByteArray *>(p); }, buf);
	emit frameUpdated();
}

} // namespace trikGui

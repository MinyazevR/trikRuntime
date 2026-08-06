#pragma once

#include <QObject>
#include <QQuickImageProvider>
#include <QImage>

namespace trikControl {
class BrickInterface;
}

namespace trikGui {

class VideoDisplayProvider : public QObject, public QQuickImageProvider
{
	Q_OBJECT

public:
	explicit VideoDisplayProvider(trikControl::BrickInterface &brick);

	QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

Q_SIGNALS:
	void frameUpdated();
	void displayStarted();
	void displayFinished();

private Q_SLOTS:
	void updateFrame(QByteArray data, uint32_t width, uint32_t height);

private:
	QImage mFrame;
};

} // namespace trikGui

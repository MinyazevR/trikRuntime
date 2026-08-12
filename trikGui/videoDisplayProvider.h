#pragma once

#include <QObject>

namespace trikControl {
class BrickInterface;
}

namespace trikGui {

/// Relays videoDisplayStarted/Finished from Brick to QML.
/// Video output is handled directly by DSP HAL FbOutput — no QPainter involved.
class VideoDisplayProvider : public QObject
{
	Q_OBJECT

public:
	explicit VideoDisplayProvider(trikControl::BrickInterface &brick);

Q_SIGNALS:
	void displayStarted();
	void displayFinished();
};

} // namespace trikGui

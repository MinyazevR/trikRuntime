#include "videoDisplayProvider.h"

#include <trikControl/brickInterface.h>
#include <QsLog.h>

namespace trikGui {

VideoDisplayProvider::VideoDisplayProvider(trikControl::BrickInterface &brick)
{
	QLOG_INFO() << "VideoDisplayProvider: created";
	connect(&brick, &trikControl::BrickInterface::videoDisplayStarted,
	        this, &VideoDisplayProvider::displayStarted,
	        Qt::QueuedConnection);
	connect(&brick, &trikControl::BrickInterface::videoDisplayFinished,
	        this, &VideoDisplayProvider::displayFinished,
	        Qt::QueuedConnection);
}

} // namespace trikGui

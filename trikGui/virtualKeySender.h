#pragma once

#include <QObject>

class QKeyEvent;

namespace trikGui {

class VirtualKeySender : public QObject
{
	Q_OBJECT

public:
	explicit VirtualKeySender(QObject *parent = nullptr);

public slots:
	void sendKeyPress(int keyCode);
};

} // namespace trikGui
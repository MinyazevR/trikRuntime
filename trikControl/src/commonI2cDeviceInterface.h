#pragma once
#include <QtCore/QString>
#include <QtCore/QMutex>
#include "i2cDeviceInterface.h"
#include <trikHal/mspI2cInterface.h>

namespace trikControl {
/// Real implementation of I2C bus communicator.
class CommonI2cDeviceInterface : public trikHal::MspI2cInterface
{
public:
	/// Reads data by given I2C command number and returns the result as QVector.
	virtual int transfer(const QVector<I2cDeviceInterface::Message> &vector) = 0;
};
}

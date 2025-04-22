#pragma once

#include <QtCore/QByteArray>
#include "commonI2cDeviceInterface.h"

namespace trikControl {

class CommonI2c: public CommonI2cDeviceInterface
{
public:

  CommonI2c(ushort regSize);

  int send(const QByteArray &data) override;

  int read(const QByteArray &data) override;

  QVector<uint8_t> readX(const QByteArray &data) override;

  int transfer(const QVector<I2cDeviceInterface::Message> &vector) override;

  bool connect(const QString &devicePath, int deviceId) override;

  void disconnect() override;

private:
  ushort mRegSize;
  int mDeviceFileDescriptor = -1;
  ushort mDeviceAddress = 0;
  int write(uchar* writeData__u8, ushort length);

};

}

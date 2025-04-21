#pragma once

#include <QtCore/QByteArray>
#include <trikHal/mspI2cInterface.h>

namespace trikControl {

/// Real implementation of I2C bus communicator.
class CommonI2c: public trikHal::MspI2cInterface
{
public:

  /// Constructor
  CommonI2c() = default;

  int send(const QByteArray &data) override;

  QVariant read(const QByteArray &data) override;

  bool connect(const QString &devicePath, int deviceId) override;

  void disconnect() override;

private:
  int mDeviceFileDescriptor = -1;
  ushort mDeviceAddress = 0;
  int write(uchar* writeData__u8, ushort length);

};

}

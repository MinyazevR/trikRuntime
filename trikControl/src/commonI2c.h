#pragma once

#include <QtCore/QByteArray>
#include <trikHal/mspI2cInterface.h>

namespace trikControl {

class CommonI2c: public trikHal::MspI2cInterface
{
public:

  /// Constructor
  CommonI2c() = default;

  int send(const QByteArray &data) override;

  int read(const QByteArray &data) override;

  QVector<uint8_t> readX(const QByteArray &data) override;

  bool connect(const QString &devicePath, int deviceId) override;

  void disconnect() override;

private:
  int mDeviceFileDescriptor = -1;
  ushort mDeviceAddress = 0;
  int write(uchar* writeData__u8, ushort length);

};

}

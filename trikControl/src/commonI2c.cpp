#include "commonI2c.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <unistd.h>
#include <QsLog.h>
#include <QVector>

using namespace trikControl;

constexpr int REG_SIZE = 2;

QVector<uint8_t> CommonI2c::readX(const QByteArray &data)
{
	if (data.size() < 4) {
		return {};
	}

	struct i2c_msg i2c_messages[2];
	struct i2c_rdwr_ioctl_data i2c_messageset[1];

	char cmd[2] = {data[1], data[0]};

	i2c_messages[0] = {mDeviceAddress, 0, REG_SIZE, (__u8*)cmd};

	ushort sizeForRead = (data[3] << 8) | data[2];
	QVector<uint8_t> vector(sizeForRead, 0);
	i2c_messages[1] = {mDeviceAddress, I2C_M_RD, sizeForRead, (__u8*)vector.data()};

	i2c_messageset[0].msgs = i2c_messages;
	i2c_messageset[0].nmsgs = 2;

	if (ioctl(mDeviceFileDescriptor, I2C_RDWR, &i2c_messageset) < 0) {
	      QLOG_INFO() << "Failed read dataSize" << sizeForRead;
	      return {};
	  }

	return vector;
}

int CommonI2c::read(const QByteArray &data)
{
	if (data.size() < 4) {
		return {};
	}

	auto vector = readX(data);
	ushort sizeForRead = (data[3] << 8) | data[2];

	if (vector.length() < sizeForRead) {
		return 0;
	}

	if (sizeForRead == 1) {
		return vector[0];
	}

	return (vector[0] << 8) | vector[1];
}

bool CommonI2c::connect(const QString &devicePath, int deviceId)
{
  mDeviceFileDescriptor = open(devicePath.toStdString().c_str(), O_RDWR);

  if (mDeviceFileDescriptor < 0) {
    QLOG_ERROR() << "Failed to open I2C device file " << devicePath;
    return false;
  }

  if (ioctl(mDeviceFileDescriptor, I2C_SLAVE, deviceId)) {
    QLOG_ERROR() << "ioctl(" << mDeviceFileDescriptor << ", I2C_SLAVE, " << deviceId << ") failed ";
    return false;
  }

  mDeviceAddress = deviceId;
  return true;
}

int CommonI2c::send(const QByteArray &data) {

	auto dataSize = data.size();

	if (dataSize < 3) {
		return -1;
	}

	if (dataSize == 3) {
		char cmd[3] = {data[1], data[0], data[2]};
		return write((__u8*)cmd, dataSize);
	}

	char cmd[4] = {data[1], data[0], data[3], data[2]};
	return write((__u8*)cmd, dataSize);
}

void CommonI2c::disconnect()
{
  close(mDeviceFileDescriptor);
}

int CommonI2c::write(__u8* writeData, __u16 length)
{
    struct i2c_msg i2c_messages[1];
    struct i2c_rdwr_ioctl_data i2c_messageset[1];

    i2c_messages[0] = {mDeviceAddress, 0, length, writeData};
    i2c_messageset[0].msgs = i2c_messages;
    i2c_messageset[0].nmsgs = 1;
    return ioctl(mDeviceFileDescriptor, I2C_RDWR, &i2c_messageset);
}

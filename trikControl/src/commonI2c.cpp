#include "commonI2c.h"
#include "stdlib.h"
#include <fcntl.h>
#include <cstring>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <unistd.h>
#include <array>
#include <QsLog.h>
#include <QVector>

using namespace trikControl;

namespace {
	static i2c_msg generateWriteMessage(__u16 deviceAddress,
					    __u16 length, __u8 *writeData) {
		struct i2c_msg i2cMsg;
		i2cMsg.addr = deviceAddress;
		i2cMsg.flags = 0;
		i2cMsg.len = length;
		i2cMsg.buf = writeData;
		return i2cMsg;
	}
}


QVariant CommonI2c::read(const QByteArray &data)
{
    if (data.size() < 4) {
	    QLOG_INFO() << "CommonI2c read with dataSize" << data.size();
	    return {};
    }

    struct i2c_msg i2c_messages[2];
    struct i2c_rdwr_ioctl_data i2c_messageset[1];

    char cmd[2] = {data[1], data[0]};

    i2c_messages[0] = generateWriteMessage(mDeviceAddress, 2, (__u8*)cmd);
    auto sizeForRead = (data[3] << 8) | data[2];

    QLOG_INFO() << "Try read dataSize" << sizeForRead;

    QVector<uint8_t> vector(sizeForRead, 0);

    i2c_messages[1].addr = mDeviceAddress;
    i2c_messages[1].flags = I2C_M_RD;
    i2c_messages[1].len = sizeForRead;
    i2c_messages[1].buf = (__u8*)vector.data();

    i2c_messageset[0].msgs = i2c_messages;
    i2c_messageset[0].nmsgs = 2;

    if (ioctl(mDeviceFileDescriptor, I2C_RDWR, &i2c_messageset) < 0) {
	QLOG_INFO() << "Failed read dataSize" << sizeForRead;
	return -1;
    }

    return QVariant::fromValue(vector);
}

int CommonI2c::write(__u8* writeData, __u16 length)
{
    struct i2c_msg i2c_messages[1];
    struct i2c_rdwr_ioctl_data i2c_messageset[1];

    i2c_messages[0] = generateWriteMessage(mDeviceAddress,
					   length,
					   writeData);
    i2c_messageset[0].msgs = i2c_messages;
    i2c_messageset[0].nmsgs = 1;

    if (ioctl(mDeviceFileDescriptor, I2C_RDWR, &i2c_messageset) < 0) {
	return -1;
    }

    return 0;
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
	} else if (dataSize == 4) {
		char cmd[4] = {data[1], data[0], data[3], data[2]};
		return write((__u8*)cmd, dataSize);
	} else {
		QByteArray newData(data);
		auto *cmd = newData.data();
		std::swap(*(cmd), *(cmd + 1));
		return write((__u8*)cmd, dataSize);
	}
	return -1;
}

void CommonI2c::disconnect()
{
  close(mDeviceFileDescriptor);
}

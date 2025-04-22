#include "commonI2c.h"
#include <fcntl.h>
#include "sys/ioctl.h"
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <unistd.h>
#include <QsLog.h>
#include <QVector>

using namespace trikControl;

CommonI2c::CommonI2c(ushort regSize) : mRegSize(regSize / 8) {}

void CommonI2c::disconnect()
{
	close(mDeviceFileDescriptor);
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


QVector<uint8_t> CommonI2c::readX(const QByteArray &data)
{
	QLOG_INFO() << "CommonI2c::readX\n";
	if (data.size() < 4) {
		return {};
	}

	struct i2c_msg i2c_messages[2];
	struct i2c_rdwr_ioctl_data i2c_messageset[1];

	QLOG_INFO() << "mRegSize:" << mRegSize;
	char cmd[2] = {0, 0};

	if  (mRegSize == 2) {
		cmd[0] = data[1];
		cmd[1] = data[0];
	} else {
		cmd[0] = data[0];
	}

	i2c_messages[0] = {mDeviceAddress, 0, mRegSize, (__u8*)cmd};
	const auto  sizeForRead = static_cast<ushort>((data[3] << 8) | data[2]);
	QLOG_INFO() << "sizeForRead:" << sizeForRead;
	QVector<uint8_t> vector(sizeForRead, 0);

	i2c_messages[1] = {mDeviceAddress, I2C_M_RD, sizeForRead, (__u8*)vector.data()};

	i2c_messageset[0].msgs = i2c_messages;
	i2c_messageset[0].nmsgs = 2;

	if (ioctl(mDeviceFileDescriptor, I2C_RDWR, &i2c_messageset) < 0) {
	      QLOG_INFO() << "Failed read dataSize" << sizeForRead;
	      return {};
	}

	QLOG_INFO() << "read success: " << sizeForRead;
	QLOG_INFO() << vector;
	QLOG_INFO() << "vectorsize: " << vector.size();
	return vector;
}

int CommonI2c::read(const QByteArray &data)
{
	if (data.size() < 4) {
		return {};
	}

	const auto sizeForRead = (ushort)((data[3] << 8) | data[2]);
	QLOG_INFO() << "WANT TO READ DATA WITH SIZE:" << sizeForRead;
	auto vector = readX(data);

	if (vector.length() < sizeForRead) {
		return 0;
	}

	if (sizeForRead == 1) {
		return vector[0];
	}

	return (vector[0] << 8) | vector[1];
}

int CommonI2c::send(const QByteArray &data) {

	auto dataSize = data.size();

	if (dataSize < 3) {
		return -1;
	}

	if (mRegSize == 1) {
		if (dataSize == 4) {
			char cmd[3] = {data[0], data[3], data[2]};
			return write((__u8*)cmd, dataSize);
		}
		char cmd[3] = {data[0], data[2]};
		return write((__u8*)cmd, dataSize);
	}

	if (dataSize == 4) {
		char cmd[4] = {data[1], data[0], data[3], data[2]};
		return write((__u8*)cmd, dataSize);
	}

	char cmd[3] = {data[1], data[0], data[2]};
	return write((__u8*)cmd, dataSize);
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

int CommonI2c::transfer(const QVector<I2cDeviceInterface::Message> &vector) {

	if (vector.size() > I2C_RDRW_IOCTL_MAX_MSGS) {
		return {};
	}

	struct i2c_msg msgs[I2C_RDRW_IOCTL_MAX_MSGS];
	struct i2c_rdwr_ioctl_data rdwr {};

	int counter = 0;
	for (auto &&message: vector) {
		__u16 flags = 0;
		if (message.type == I2cDeviceInterface::MessageType::read) {
			flags |= I2C_M_RD;
		}
		auto data = message.data;
		msgs[counter].addr = mDeviceAddress;
		msgs[counter].flags = flags;
		auto lenght = data.size();
		msgs[counter].len = lenght;
		msgs[counter].buf = (__u8*)data.data();
	}

	rdwr.msgs = msgs;
	rdwr.nmsgs = counter;
	return ioctl(mDeviceFileDescriptor, I2C_RDWR, &rdwr);
}

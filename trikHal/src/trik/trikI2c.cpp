/* Copyright 2015 Yurii Litvinov and CyberTech Labs Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#include "trikI2c.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <unistd.h>
#include <array>

#include <QsLog.h>

using namespace trikHal::trik;

static inline __s32 i2c_smbus_access(int file, __u8 read_write, __u8 command
		, __u32 size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args {};
	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;
	return ioctl(file, I2C_SMBUS, &args);
}

static inline __s32 i2c_smbus_read_word_data(int file, __u8 command)
{
	union i2c_smbus_data data {};
	if (i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_WORD_DATA, &data)) {
		return -1;
	}
	return 0x0FFFF & data.word;
}

static inline __s32 i2c_smbus_read_byte_data(int file, __u8 command)
{
	union i2c_smbus_data data {};

	if (i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_BYTE_DATA, &data)) {
		return -1;
	}

	return 0x0FF & data.byte;
}

static inline __s32 i2c_smbus_read_i2c_block_data(int file, __u8 command, __u8 length, __u8 *values)
{
	union i2c_smbus_data data {};

	if (length > I2C_SMBUS_BLOCK_MAX) {
		length = I2C_SMBUS_BLOCK_MAX;
	}

	data.block[0] = length;
	if (i2c_smbus_access(file, I2C_SMBUS_READ, command
			, length == 32 ? I2C_SMBUS_I2C_BLOCK_BROKEN : I2C_SMBUS_I2C_BLOCK_DATA
			, &data))
	{
			return -1;
	}

	for (int i = 1; i <= data.block[0]; i++) {
			values[i - 1] = data.block[i];
	}

	return data.block[0];
}

static inline __s32 i2c_smbus_write_word_data(int file, __u8 command, __u16 value)
{
	union i2c_smbus_data data {};
	data.word = value;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_WORD_DATA, &data);
}

static inline __s32 i2c_smbus_write_byte_data(int file, __u8 command, __u8 value)
{
	union i2c_smbus_data data {};
	data.byte = value;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_BYTE_DATA, &data);
}

TrikI2c::~TrikI2c()
{
	disconnect();
}

bool TrikI2c::checkFuncs(int size) {
	if (!(mFuncs & size)) {
		QLOG_ERROR() << "Device with file descriptor "
			     << mDeviceFileDescriptor << "doesnot support " << size << "operation";
		return false;
	}
	return true;
}

void TrikI2c::send(const QByteArray &data)
{
	if (data.size() == 3) {
		if (!checkFuncs(I2C_FUNC_SMBUS_WRITE_BYTE_DATA)) {
			return;
		}
		i2c_smbus_write_byte_data(mDeviceFileDescriptor, data[0], data[2]);
	} else {
		if (!checkFuncs(I2C_FUNC_SMBUS_WRITE_WORD_DATA)) {
			return;
		}
		i2c_smbus_write_word_data(mDeviceFileDescriptor, data[0], data[2] | (data[3] << 8));
	}
}

int TrikI2c::read(const QByteArray &data)
{
	if (data.size() == 1) {
		if (!checkFuncs(I2C_FUNC_SMBUS_READ_BYTE_DATA)) {
			return -1;
		}
		return i2c_smbus_read_byte_data(mDeviceFileDescriptor, data[0]);
	}
	if (data.size() == 2) {
		if (!checkFuncs(I2C_FUNC_SMBUS_READ_WORD_DATA)) {
			return -1;
		}
		return i2c_smbus_read_word_data(mDeviceFileDescriptor, data[0]);
	}
	if (!checkFuncs(I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
		return -1;
	}
	std::array<uint8_t, 4> buffer {};
	i2c_smbus_read_i2c_block_data(mDeviceFileDescriptor, data[0], 4, buffer.data());
	return buffer[3] << 24 | buffer[2] <<  16 | buffer[1] << 8 | buffer[0];
}

bool TrikI2c::connect(const QString &devicePath, int deviceId)
{
	mDeviceFileDescriptor = open(devicePath.toStdString().c_str(), O_RDWR);
	if (mDeviceFileDescriptor < 0) {
		QLOG_ERROR() << "Failed to open I2C device file " << devicePath;
		return false;
	}

	if (ioctl(mDeviceFileDescriptor, I2C_FUNCS, &mFuncs)) {
		QLOG_ERROR() << "Failed to get I2C_FUNCS for " << devicePath;
		return false;
	}

	if (ioctl(mDeviceFileDescriptor, I2C_SLAVE, deviceId)) {
		QLOG_ERROR() << "ioctl(" << mDeviceFileDescriptor << ", I2C_SLAVE, " << deviceId << ") failed ";
		return false;
	}

	return true;
}

void TrikI2c::disconnect()
{
	close(mDeviceFileDescriptor);
}

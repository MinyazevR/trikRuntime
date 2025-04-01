#include <trikHal/mspI2cInterface.h>
#include "i2cDevice.h"
#include <QsLog.h>
using namespace trikControl;

I2cDevice::I2cDevice(const trikKernel::Configurer &configurer, trikHal::MspI2cInterface *i2c, int bus, int address)
	: mState("I2cDevice")
	, mInterface(i2c)
	, mCommunicator(new MspI2cCommunicator(configurer, *i2c, bus, address))
{
	mState.ready();
}

I2cDevice::~I2cDevice() {}

I2cDevice::Status I2cDevice::status() const
{
	return combine(*mCommunicator, mState.status());
}

int I2cDevice::send(int reg, int value, const QString &mode) {

	if (status() != DeviceInterface::Status::ready) {
		return -1;
	}

	QByteArray command;

	command.append(static_cast<char>(reg & 0xFF));
	command.append(static_cast<char>((reg >> 8) & 0xFF));
	command.append(static_cast<char>(value & 0xFF));

	if (mode == "b") {
		return mCommunicator->send(command);
	}

	if (mode == "w") {
		command.append(static_cast<char>((value >> 8) & 0xFF));
		return mCommunicator->send(command);
	}
	return -1;
}

int I2cDevice::sendX(int reg, const QVector<uint8_t> &data) {

	if (status() != DeviceInterface::Status::ready) {
		return -1;
	}

	QByteArray command;
	command.append(static_cast<char>(reg & 0xFF));
	command.append(static_cast<char>((reg >> 8) & 0xFF));
	command.append((const char*)(data.data()));

	return mCommunicator->send(command);
}

int I2cDevice::read(int reg, const QString &mode) {
	QLOG_INFO() << "TRY To send byte with mode" << mode << "\n";
	QByteArray command;

	command.append(static_cast<char>(reg & 0xFF));
	command.append(static_cast<char>((reg >> 8) & 0xFF));

	if (mode == "b") {
		QLOG_INFO() << "MODE B";
		command.append(static_cast<char>(0x01));
		command.append(static_cast<char>(0x00));
		return mCommunicator->read(command).toInt();
	} else if (mode == "w") {
		QLOG_INFO() << "MODE W";
		command.append(static_cast<char>(0x02));
		command.append(static_cast<char>(0x00));
		return mCommunicator->read(command).toInt();
	}
	QLOG_INFO() << "NON EXISTING MODE";
	return -1;
}

QVector<uint8_t> I2cDevice::readX(int reg, int size) {

	if (status() != DeviceInterface::Status::ready) {
		return {};
	}

	QByteArray command;
	command.append(static_cast<char>(reg & 0xFF));
	command.append(static_cast<char>((reg >> 8) & 0xFF));
	command.append(static_cast<char>(size & 0xFF));
	command.append(static_cast<char>((size >> 8) & 0xFF));

	auto result = mCommunicator->read(command);
	return result.value<QVector<uint8_t>>();
}


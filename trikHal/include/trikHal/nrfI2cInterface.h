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

#pragma once

#include <QtCore/QByteArray>
#include <trikHal/trikHalDeclSpec.h>
#include <QString>
#include <QVector>

namespace trikHal {

/// Communicates with nRF periphery module over I2C bus.
class TRIKHAL_EXPORT NrfI2cInterface
{
	Q_DISABLE_COPY(NrfI2cInterface)
public:
	struct Message {
		QString type; // "read" or "write"
		QVector<uint8_t> data;
	};

	NrfI2cInterface() = default;

	virtual ~NrfI2cInterface() = default;

	virtual int send(const QByteArray &data) = 0;
	virtual int read(const QByteArray &data) = 0;
	virtual QVector<uint8_t> readX(const QByteArray &data) = 0;
	virtual bool connect(const QString &devicePath, int deviceId) = 0;
	virtual int transfer(const QVector<NrfI2cInterface::Message> &vector) = 0;
	virtual void disconnect() = 0;
};

}

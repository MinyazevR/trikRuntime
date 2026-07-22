/* Copyright 2026 CyberTech Labs Ltd.
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

#include "ros2Config.h"

#include <QtCore/QFile>
#include <QtXml/QDomDocument>
#include <QsLog.h>

namespace trikRos2 {

Ros2Config Ros2Config::defaults()
{
	Ros2Config cfg;

	cfg.sensors = {
		{"A1", "sensors/a1"},
		{"A2", "sensors/a2"},
		{"A3", "sensors/a3"},
		{"A4", "sensors/a4"},
		{"A5", "sensors/a5"},
		{"A6", "sensors/a6"},
		{"D1", "sensors/d1"},
		{"D2", "sensors/d2"},
		{"D3", "sensors/d3"},
	};

	cfg.encoders = {
		{"E1", "encoders/e1"},
		{"E2", "encoders/e2"},
		{"E3", "encoders/e3"},
		{"E4", "encoders/e4"},
	};

	cfg.motors = {
		{"M1", "motors/m1"},
		{"M2", "motors/m2"},
		{"M3", "motors/m3"},
		{"M4", "motors/m4"},
	};

	cfg.servos = {
		{"S1", "servos/s1"},
		{"S2", "servos/s2"},
		{"S3", "servos/s3"},
		{"S4", "servos/s4"},
		{"S5", "servos/s5"},
		{"S6", "servos/s6"},
	};

	cfg.imu.accelerometerEnabled = true;
	cfg.imu.accelerometerTopic   = "imu/accelerometer";
	cfg.imu.gyroscopeEnabled     = true;
	cfg.imu.gyroscopeTopic       = "imu/gyroscope";

	cfg.rateHz = 20.0;

	return cfg;
}

Ros2Config Ros2Config::loadFromFile(const std::string & path)
{
	QFile file(QString::fromStdString(path));
	if (!file.open(QIODevice::ReadOnly)) {
		QLOG_WARN() << "trikRos2: cannot open config" << QString::fromStdString(path)
		            << "— using defaults (all ports enabled)";
		return defaults();
	}

	QDomDocument doc;
	QString parseError;
	int parseErrorLine = 0;
	if (!doc.setContent(&file, &parseError, &parseErrorLine)) {
		QLOG_ERROR() << "trikRos2: XML parse error at line" << parseErrorLine
		             << ":" << parseError << "— using defaults";
		return defaults();
	}

	const QDomElement root = doc.documentElement();
	if (root.tagName() != "ros2config") {
		QLOG_ERROR() << "trikRos2: expected root element <ros2config>, got <"
		             << root.tagName() << "> — using defaults";
		return defaults();
	}

	Ros2Config cfg;

	// <params rate_hz="20.0" config_path="/home/root/trik"/>
	const QDomElement params = root.firstChildElement("params");
	if (!params.isNull()) {
		if (params.hasAttribute("rate_hz"))
			cfg.rateHz = params.attribute("rate_hz").toDouble();
	}

	// <sensors> <sensor port="A1" topic="sensors/a1"/> ...
	for (QDomElement el = root.firstChildElement("sensors").firstChildElement("sensor");
	     !el.isNull();
	     el = el.nextSiblingElement("sensor"))
	{
		cfg.sensors.push_back({
			el.attribute("port").toStdString(),
			el.attribute("topic").toStdString()
		});
	}

	// <encoders> <encoder port="E1" topic="encoders/e1"/> ...
	for (QDomElement el = root.firstChildElement("encoders").firstChildElement("encoder");
	     !el.isNull();
	     el = el.nextSiblingElement("encoder"))
	{
		cfg.encoders.push_back({
			el.attribute("port").toStdString(),
			el.attribute("topic").toStdString()
		});
	}

	// <motors> <motor port="M1" topic="motors/m1"/> ...
	for (QDomElement el = root.firstChildElement("motors").firstChildElement("motor");
	     !el.isNull();
	     el = el.nextSiblingElement("motor"))
	{
		cfg.motors.push_back({
			el.attribute("port").toStdString(),
			el.attribute("topic").toStdString()
		});
	}

	// <servos> <servo port="S1" topic="servos/s1"/> ...
	for (QDomElement el = root.firstChildElement("servos").firstChildElement("servo");
	     !el.isNull();
	     el = el.nextSiblingElement("servo"))
	{
		cfg.servos.push_back({
			el.attribute("port").toStdString(),
			el.attribute("topic").toStdString()
		});
	}

	// <imu>
	//   <accelerometer topic="imu/accelerometer"/>   <!-- element missing = disabled -->
	//   <gyroscope     topic="imu/gyroscope"/>
	// </imu>
	const QDomElement imuEl = root.firstChildElement("imu");
	if (!imuEl.isNull()) {
		const QDomElement accel = imuEl.firstChildElement("accelerometer");
		cfg.imu.accelerometerEnabled = !accel.isNull();
		if (cfg.imu.accelerometerEnabled && accel.hasAttribute("topic"))
			cfg.imu.accelerometerTopic = accel.attribute("topic").toStdString();

		const QDomElement gyro = imuEl.firstChildElement("gyroscope");
		cfg.imu.gyroscopeEnabled = !gyro.isNull();
		if (cfg.imu.gyroscopeEnabled && gyro.hasAttribute("topic"))
			cfg.imu.gyroscopeTopic = gyro.attribute("topic").toStdString();
	} else {
		cfg.imu.accelerometerEnabled = false;
		cfg.imu.gyroscopeEnabled     = false;
	}

	QLOG_INFO() << "trikRos2: loaded config from" << QString::fromStdString(path)
	            << "— sensors:" << cfg.sensors.size()
	            << "encoders:" << cfg.encoders.size()
	            << "motors:" << cfg.motors.size()
	            << "servos:" << cfg.servos.size()
	            << "rate:" << cfg.rateHz << "Hz";
	return cfg;
}

} // namespace trikRos2

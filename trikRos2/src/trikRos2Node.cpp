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

#include "trikRos2Node.h"

#include <chrono>
#include <QsLog.h>

using namespace std::chrono_literals;

namespace trikRos2 {

TrikRos2Node::TrikRos2Node(trikControl::BrickInterface * brick, const Ros2Config & config)
	: rclcpp::Node("trik")
{
	QLOG_INFO() << "trikRos2: initializing node";

	initSensors(brick, config);
	initEncoders(brick, config);
	initMotors(brick, config);
	initServos(brick, config);
	initImu(brick, config);

	const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::duration<double>(1.0 / config.rateHz));

	timer_ = this->create_wall_timer(period, std::bind(&TrikRos2Node::publishAll, this));

	QLOG_INFO() << "trikRos2: node ready —"
	            << sensors_.size()  << "sensors,"
	            << encoders_.size() << "encoders,"
	            << motors_.size()   << "motors,"
	            << servos_.size()   << "servos,"
	            << (accelerometer_ ? "accel," : "no accel,")
	            << (gyroscope_     ? "gyro,"  : "no gyro,")
	            << config.rateHz   << "Hz";
}

void TrikRos2Node::initSensors(trikControl::BrickInterface * brick, const Ros2Config & config)
{
	for (const auto & sc : config.sensors) {
		try {
			sensors_.push_back(
				std::make_unique<SensorPublisher>(this, brick, sc.port, sc.topic));
		} catch (const std::exception & e) {
			QLOG_WARN() << "trikRos2: sensor" << QString::fromStdString(sc.port)
			            << "skipped:" << e.what();
		}
	}
}

void TrikRos2Node::initEncoders(trikControl::BrickInterface * brick, const Ros2Config & config)
{
	for (const auto & ec : config.encoders) {
		try {
			encoders_.push_back(
				std::make_unique<EncoderPublisher>(this, brick, ec.port, ec.topic));
		} catch (const std::exception & e) {
			QLOG_WARN() << "trikRos2: encoder" << QString::fromStdString(ec.port)
			            << "skipped:" << e.what();
		}
	}
}

void TrikRos2Node::initMotors(trikControl::BrickInterface * brick, const Ros2Config & config)
{
	for (const auto & mc : config.motors) {
		try {
			motors_.push_back(
				std::make_unique<MotorSubscriber>(this, brick, mc.port, mc.topic));
		} catch (const std::exception & e) {
			QLOG_WARN() << "trikRos2: motor" << QString::fromStdString(mc.port)
			            << "skipped:" << e.what();
		}
	}
}

void TrikRos2Node::initServos(trikControl::BrickInterface * brick, const Ros2Config & config)
{
	for (const auto & sc : config.servos) {
		try {
			servos_.push_back(
				std::make_unique<MotorSubscriber>(this, brick, sc.port, sc.topic));
		} catch (const std::exception & e) {
			QLOG_WARN() << "trikRos2: servo" << QString::fromStdString(sc.port)
			            << "skipped:" << e.what();
		}
	}
}

void TrikRos2Node::initImu(trikControl::BrickInterface * brick, const Ros2Config & config)
{
	if (config.imu.accelerometerEnabled) {
		try {
			accelerometer_ = std::make_unique<AccelerometerPublisher>(
				this, brick, config.imu.accelerometerTopic);
		} catch (const std::exception & e) {
			QLOG_WARN() << "trikRos2: accelerometer skipped:" << e.what();
		}
	}

	if (config.imu.gyroscopeEnabled) {
		try {
			gyroscope_ = std::make_unique<GyroscopePublisher>(
				this, brick, config.imu.gyroscopeTopic);
		} catch (const std::exception & e) {
			QLOG_WARN() << "trikRos2: gyroscope skipped:" << e.what();
		}
	}
}

void TrikRos2Node::publishAll()
{
	for (auto & s : sensors_)  { s->publish(); }
	for (auto & e : encoders_) { e->publish(); }
	if (accelerometer_) { accelerometer_->publish(); }
	if (gyroscope_)     { gyroscope_->publish(); }
}

} // namespace trikRos2

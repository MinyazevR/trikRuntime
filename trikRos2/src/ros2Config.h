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

#pragma once

#include <string>
#include <vector>

namespace trikRos2 {

// A sensor on an analog (A1-A6) or digital (D1-D3) port.
// Data is published to the host.
struct SensorConfig {
	std::string port;   // "A1".."A6", "D1".."D3"
	std::string topic;  // ROS 2 topic, e.g. "sensors/a1"
};

// An encoder (E1-E4). Data is published to the host.
struct EncoderConfig {
	std::string port;   // "E1".."E4"
	std::string topic;  // "encoders/e1"
};

// A motor (M1-M4). The host sends power in -100..100.
struct MotorConfig {
	std::string port;   // "M1".."M4"
	std::string topic;  // "motors/m1"
};

// A servo (S1-S6). The host sends power/angle in -100..100.
struct ServoConfig {
	std::string port;   // "S1".."S6"
	std::string topic;  // "servos/s1"
};

// The accelerometer and gyroscope are accessed via brick->accelerometer() /
// brick->gyroscope() — they have no ports, only topics and an enable flag.
struct ImuConfig {
	bool        accelerometerEnabled = true;
	std::string accelerometerTopic   = "imu/accelerometer";
	bool        gyroscopeEnabled     = true;
	std::string gyroscopeTopic       = "imu/gyroscope";
};

struct Ros2Config {
	std::vector<SensorConfig>  sensors;   // A1-A6, D1-D3
	std::vector<EncoderConfig> encoders;  // E1-E4
	std::vector<MotorConfig>   motors;    // M1-M4
	std::vector<ServoConfig>   servos;    // S1-S6
	ImuConfig                  imu;       // accelerometer and gyroscope

	double rateHz = 20.0;

	// Loads config from an XML file. Returns defaults() on error.
	static Ros2Config loadFromFile(const std::string & path);

	// Default configuration — all ports enabled.
	// Used when the XML file is missing or corrupted.
	static Ros2Config defaults();
};

} // namespace trikRos2

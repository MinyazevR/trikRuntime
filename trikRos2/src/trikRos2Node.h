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

#include <memory>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <trikControl/brickInterface.h>

#include "ros2Config.h"
#include "sensorPublisher.h"
#include "encoderPublisher.h"
#include "motorSubscriber.h"
#include "accelerometerPublisher.h"
#include "gyroscopePublisher.h"

namespace trikRos2 {

// ROS 2 node integrated into the trikGui process.
// Does not create or own the Brick — receives an already-existing BrickInterface*
// from trikGui. The destructor does NOT call brick->stop(): trikGui handles that.
class TrikRos2Node : public rclcpp::Node
{
public:
	// brick must stay alive for the whole lifetime of this node.
	TrikRos2Node(trikControl::BrickInterface * brick, const Ros2Config & config);
	~TrikRos2Node() override = default;

private:
	void initSensors(trikControl::BrickInterface * brick, const Ros2Config & config);
	void initEncoders(trikControl::BrickInterface * brick, const Ros2Config & config);
	// Motors (M1-M4) and servos (S1-S6) share the same MotorSubscriber,
	// since both implement MotorInterface::setPower(int).
	void initMotors(trikControl::BrickInterface * brick, const Ros2Config & config);
	void initServos(trikControl::BrickInterface * brick, const Ros2Config & config);
	void initImu(trikControl::BrickInterface * brick, const Ros2Config & config);

	// Called on a timer — publishes all sensors and encoders
	void publishAll();

	std::vector<std::unique_ptr<SensorPublisher>>  sensors_;
	std::vector<std::unique_ptr<EncoderPublisher>> encoders_;
	std::vector<std::unique_ptr<MotorSubscriber>>  motors_;   // M1-M4
	std::vector<std::unique_ptr<MotorSubscriber>>  servos_;   // S1-S6
	std::unique_ptr<AccelerometerPublisher>        accelerometer_;
	std::unique_ptr<GyroscopePublisher>            gyroscope_;

	rclcpp::TimerBase::SharedPtr timer_;
};

} // namespace trikRos2

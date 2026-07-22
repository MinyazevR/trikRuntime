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

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int32.hpp>
#include <trikControl/brickInterface.h>
#include <trikControl/motorInterface.h>

namespace trikRos2 {

// One instance = one motor on one port.
// Subscribes to std_msgs/Int32 (power -100..100) and calls setPower().
class MotorSubscriber
{
public:
	MotorSubscriber(
		rclcpp::Node * node,
		trikControl::BrickInterface * brick,
		const std::string & port,
		const std::string & topic);

private:
	const std::string port_;
	const std::string topic_;
	trikControl::MotorInterface * const device_;
	// queue=1: only the latest command matters
	rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr subscription_;

	void onPower(const std_msgs::msg::Int32::SharedPtr msg);
};

} // namespace trikRos2

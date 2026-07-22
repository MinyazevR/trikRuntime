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
#include <sensor_msgs/msg/range.hpp>
#include <trikControl/brickInterface.h>
#include <trikControl/encoderInterface.h>

namespace trikRos2 {

// One instance = one encoder on one port.
// Publishes sensor_msgs/Range (the range field holds the value in degrees).
// publish() is called from TrikRos2Node::publishAll() on a timer.
class EncoderPublisher
{
public:
	EncoderPublisher(
		rclcpp::Node * node,
		trikControl::BrickInterface * brick,
		const std::string & port,
		const std::string & topic);

	void publish();

private:
	const std::string port_;
	const std::string topic_;
	trikControl::EncoderInterface * const device_;
	rclcpp::Publisher<sensor_msgs::msg::Range>::SharedPtr publisher_;
	rclcpp::Clock::SharedPtr clock_;
};

} // namespace trikRos2

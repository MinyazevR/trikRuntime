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
#include <geometry_msgs/msg/vector3_stamped.hpp>
#include <trikControl/brickInterface.h>
#include <trikControl/gyroSensorInterface.h>

namespace trikRos2 {

// Publishes gyroscope data as geometry_msgs/Vector3Stamped.
// Uses read() (processed data), not readRawData().
// publish() is called from TrikRos2Node::publishAll() on a timer.
class GyroscopePublisher
{
public:
	GyroscopePublisher(
		rclcpp::Node * node,
		trikControl::BrickInterface * brick,
		const std::string & topic);

	void publish();

private:
	const std::string topic_;
	trikControl::GyroSensorInterface * const device_;
	rclcpp::Publisher<geometry_msgs::msg::Vector3Stamped>::SharedPtr publisher_;
	rclcpp::Clock::SharedPtr clock_;
};

} // namespace trikRos2

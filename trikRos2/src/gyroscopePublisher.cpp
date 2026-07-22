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

#include "gyroscopePublisher.h"

namespace trikRos2 {

GyroscopePublisher::GyroscopePublisher(
	rclcpp::Node * node,
	trikControl::BrickInterface * brick,
	const std::string & topic)
	: topic_(topic)
	, device_(brick->gyroscope())
{
	if (!device_) {
		RCLCPP_ERROR(node->get_logger(),
			"GyroscopePublisher: gyroscope not available in Brick");
		throw std::runtime_error("GyroscopePublisher: no gyroscope");
	}

	RCLCPP_INFO(node->get_logger(),
		"GyroscopePublisher: -> topic '%s'", topic.c_str());

	publisher_ = node->create_publisher<geometry_msgs::msg::Vector3Stamped>(topic, 10);
	clock_     = node->get_clock();
}

void GyroscopePublisher::publish()
{
	const QVector<int> data = device_->read();
	if (data.size() < 3) {
		return;
	}

	auto msg = std::make_unique<geometry_msgs::msg::Vector3Stamped>();
	msg->header.stamp = clock_->now();
	msg->vector.x     = static_cast<double>(data[0]);
	msg->vector.y     = static_cast<double>(data[1]);
	msg->vector.z     = static_cast<double>(data[2]);

	publisher_->publish(std::move(msg));
}

} // namespace trikRos2

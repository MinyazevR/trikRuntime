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

#include "motorSubscriber.h"

#include <QtCore/QString>

namespace trikRos2 {

MotorSubscriber::MotorSubscriber(
	rclcpp::Node * node,
	trikControl::BrickInterface * brick,
	const std::string & port,
	const std::string & topic)
	: port_(port)
	, topic_(topic)
	, device_(brick->motor(QString::fromStdString(port)))
{
	if (!device_) {
		RCLCPP_ERROR(node->get_logger(),
			"MotorSubscriber: motor on port %s not found in Brick", port.c_str());
		throw std::runtime_error("MotorSubscriber: no motor on port " + port);
	}

	RCLCPP_INFO(node->get_logger(),
		"MotorSubscriber: port=%s subscribing to topic '%s'",
		port.c_str(), topic.c_str());

	subscription_ = node->create_subscription<std_msgs::msg::Int32>(
		topic,
		1,
		std::bind(&MotorSubscriber::onPower, this, std::placeholders::_1));
}

void MotorSubscriber::onPower(const std_msgs::msg::Int32::SharedPtr msg)
{
	device_->setPower(msg->data);
}

} // namespace trikRos2

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

#include "sensorPublisher.h"

#include <QtCore/QString>

namespace trikRos2 {

SensorPublisher::SensorPublisher(
	rclcpp::Node * node,
	trikControl::BrickInterface * brick,
	const std::string & port,
	const std::string & topic)
	: port_(port)
	, topic_(topic)
	, device_(brick->sensor(QString::fromStdString(port)))
{
	if (!device_) {
		RCLCPP_ERROR(node->get_logger(),
			"SensorPublisher: sensor on port %s not found in Brick", port.c_str());
		throw std::runtime_error("SensorPublisher: no device on port " + port);
	}

	minRange_ = static_cast<float>(device_->minValue()) * 0.01f;
	maxRange_ = static_cast<float>(device_->maxValue()) * 0.01f;

	RCLCPP_INFO(node->get_logger(),
		"SensorPublisher: port=%s -> topic '%s' [%.2f..%.2f m]",
		port.c_str(), topic.c_str(), minRange_, maxRange_);

	publisher_ = node->create_publisher<sensor_msgs::msg::Range>(topic, 10);
	clock_     = node->get_clock();
}

void SensorPublisher::publish()
{
	const int raw = device_->read();
	const float rangeM = static_cast<float>(raw) * 0.01f;

	auto msg = std::make_unique<sensor_msgs::msg::Range>();
	msg->header.stamp   = clock_->now();
	msg->radiation_type = sensor_msgs::msg::Range::INFRARED;
	msg->field_of_view  = 0.1f;
	msg->range          = rangeM;
	msg->min_range      = minRange_;
	msg->max_range      = maxRange_;

	publisher_->publish(std::move(msg));
}

} // namespace trikRos2

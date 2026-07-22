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

#include "encoderPublisher.h"

#include <QtCore/QString>

namespace trikRos2 {

EncoderPublisher::EncoderPublisher(
	rclcpp::Node * node,
	trikControl::BrickInterface * brick,
	const std::string & port,
	const std::string & topic)
	: port_(port)
	, topic_(topic)
	, device_(brick->encoder(QString::fromStdString(port)))
{
	if (!device_) {
		RCLCPP_ERROR(node->get_logger(),
			"EncoderPublisher: encoder on port %s not found in Brick", port.c_str());
		throw std::runtime_error("EncoderPublisher: no encoder on port " + port);
	}

	RCLCPP_INFO(node->get_logger(),
		"EncoderPublisher: port=%s -> topic '%s'",
		port.c_str(), topic.c_str());

	publisher_ = node->create_publisher<sensor_msgs::msg::Range>(topic, 10);
	clock_     = node->get_clock();
}

void EncoderPublisher::publish()
{
	// Encoder value in degrees is packed into the `range` field of sensor_msgs/Range.
	// The host-side listener must cast it back to int.
	//
	// EncoderInterface::read() is an unbounded tick counter converted to degrees
	// (see Encoder::read() in trikControl) — it only resets via an explicit reset()
	// call, so there is no real min/max to report. min_range/max_range are left at
	// 0 rather than an invented constant.
	auto msg = std::make_unique<sensor_msgs::msg::Range>();
	msg->header.stamp   = clock_->now();
	msg->radiation_type = sensor_msgs::msg::Range::INFRARED;
	msg->field_of_view  = 0.0f;
	msg->range          = static_cast<float>(device_->read());
	msg->min_range      = 0.0f;
	msg->max_range      = 0.0f;

	publisher_->publish(std::move(msg));
}

} // namespace trikRos2

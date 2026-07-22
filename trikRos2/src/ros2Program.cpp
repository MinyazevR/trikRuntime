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

#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <stdexcept>

#include <rclcpp/rclcpp.hpp>
#include <trikScriptRunner/trikCppProgram.h>
#include <trikKernel/paths.h>
#include <QsLog.h>

#include "ros2Config.h"
#include "trikRos2Node.h"

class Ros2Program : public trikScriptRunner::TrikCppProgram
{
public:
	using TrikCppProgram::TrikCppProgram;

	void startProgram(const std::atomic<bool> & isTerminate) override
	{
		const std::string configPath =
			trikKernel::Paths::configsPath().toStdString() + "ros2_config.xml";
		const trikRos2::Ros2Config config = trikRos2::Ros2Config::loadFromFile(configPath);

		if (!rclcpp::ok()) {
			rclcpp::init(0, nullptr);
			QLOG_INFO() << "trikRos2: rclcpp initialized";
		}

		auto node = std::make_shared<trikRos2::TrikRos2Node>(mBrick, config);

		rclcpp::executors::SingleThreadedExecutor executor;
		executor.add_node(node);

		std::thread rosThread([&executor]() {
			executor.spin();
		});

		while (!isTerminate) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		QLOG_INFO() << "trikRos2: stopping";

		executor.cancel();

		if (rosThread.joinable()) {
			rosThread.join();  // wait for spin() to finish before touching the node
		}

		node.reset();          // now safe: the thread holds no references

		rclcpp::shutdown();
		QLOG_INFO() << "trikRos2: stopped";
	}
};

TRIK_REGISTER_CPP_PROGRAM(Ros2Program)

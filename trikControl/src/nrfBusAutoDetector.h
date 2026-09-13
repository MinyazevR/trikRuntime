/* Copyright 2025 CyberTech Labs Ltd.
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

#include "peripheryCommunicatorInterface.h"

namespace trikHal {
class HardwareAbstractionInterface;
}

namespace trikKernel {
class Configurer;
}

namespace trikControl {

/// Factory that creates NrfUsbCommunicator from the nrfUsb config node.
/// Returns nullptr if the node is not present.
class NrfBusAutoDetector
{
public:
	static PeripheryCommunicatorInterface *createCommunicator(
			const trikKernel::Configurer &configurer
			, trikHal::HardwareAbstractionInterface &hardwareAbstraction);
};

}
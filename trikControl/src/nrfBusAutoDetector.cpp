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

#include "nrfBusAutoDetector.h"

#include <trikHal/hardwareAbstractionInterface.h>
#include <trikKernel/configurer.h>
#include <trikKernel/exceptions/malformedConfigException.h>
#include <QsLog.h>

#include "nrfI2cCommunicator.h"
#include "nrfUsbCommunicator.h"

using namespace trikControl;

PeripheryCommunicatorInterface *NrfBusAutoDetector::createCommunicator(
		const trikKernel::Configurer &configurer
		, trikHal::HardwareAbstractionInterface &hardwareAbstraction)
{
	// Primary: nRF USB if <nrfUsb> is configured.
	try {
		const auto vendorIdStr = configurer.attributeByDevice("nrfUsb", "vendorId");
		const auto productIdStr = configurer.attributeByDevice("nrfUsb", "productId");
		const auto outEndpointStr = configurer.attributeByDevice("nrfUsb", "outEndpoint");
		const auto inEndpointStr = configurer.attributeByDevice("nrfUsb", "inEndpoint");
		const auto telemetryPeriodStr = configurer.attributeByDevice("nrfUsb", "telemetryPeriodUs");

		bool ok = false;
		const auto vendorId = static_cast<std::uint16_t>(vendorIdStr.toUInt(&ok, 16));
		const auto productId = static_cast<std::uint16_t>(productIdStr.toUInt(&ok, 16));
		const auto outEndpoint = static_cast<std::uint8_t>(outEndpointStr.toUInt(&ok, 16));
		const auto inEndpoint = static_cast<std::uint8_t>(inEndpointStr.toUInt(&ok, 16));
		const auto telemetryPeriodUs = telemetryPeriodStr.toUInt(&ok, 10);

		std::unique_ptr<trikHal::UsbDeviceInterface> usb(
				hardwareAbstraction.createUsbDevice(vendorId, productId, outEndpoint, inEndpoint));

		auto *nrf = new NrfUsbCommunicator(std::move(usb), telemetryPeriodUs);
		nrf->connect();
		QLOG_INFO() << "NrfBusAutoDetector: using USB nRF communicator";
		return nrf;
	} catch (const trikKernel::MalformedConfigException &) {
		// Fall through to I2C.
	}

	// Fallback: nRF via I2C (legacy path, no telemetry streaming).
	QLOG_INFO() << "NrfBusAutoDetector: using I2C nRF communicator";
	return new NrfI2cCommunicator(configurer, hardwareAbstraction.nrfI2c());
}
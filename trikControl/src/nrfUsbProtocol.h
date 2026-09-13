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

#include <cstddef>
#include <cstdint>

namespace trikControl {

/// Protocol constants for nRF52833 periphery module USB communication.
/// Mirror of trik-nrf52833/nrfUsbProtocol.h.
namespace nrfUsbProtocol {

/// CommHeader wire format (LE, packed).
#pragma pack(push, 1)
struct CommHeader {
	uint8_t type;
	uint8_t flags;
	uint8_t reqId;
	uint8_t seqId;
	uint16_t len;
};
#pragma pack(pop)

static_assert(sizeof(CommHeader) == 6, "CommHeader must be 6 bytes");

/// type
constexpr uint8_t kTypePeriphery = 0x01;

/// reqId
constexpr uint8_t kReqIdTelemetry = 0x00;
constexpr uint8_t kReqIdFireAndForget = 0xFE;

/// device ids
constexpr uint8_t kDeviceLoopback = 0x00;
constexpr uint8_t kDevicePwm = 0x03;
constexpr uint8_t kDeviceControl = 0x06;
constexpr uint8_t kDevicePort = 0x07;

/// port ids for kDevicePort
constexpr uint8_t kPortGetValue = 0x01;
constexpr uint8_t kPortSetValue = 0x02;
constexpr uint8_t kPortReset = 0x03;

/// payload constants
constexpr std::size_t kHeaderSize = 6;
constexpr std::size_t kTelemetryFixed = 1 + 8;   // control byte + ts_us u64
constexpr std::size_t kTelemetryPortCount = 14;
constexpr std::size_t kTelemetryPayloadSize = 1 + 8 + kTelemetryPortCount * 4; // 65
constexpr std::size_t kTelemetryFrameSize = kHeaderSize + kTelemetryPayloadSize; // 71

/// Port table indices (mirror nRF52833 firmware port table).
constexpr int kEncoderBase = 0;
constexpr int kAnalogBase = 4;
constexpr int kServoBase = 10;

} // namespace nrfUsbProtocol

} // namespace trikControl
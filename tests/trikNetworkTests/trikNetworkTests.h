/* Copyright 2024 CyberTech Labs Ltd.
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

#include <trikControl/brickInterface.h>
#include <trikNetwork/mailboxFactory.h>
#include <trikNetwork/mailboxInterface.h>
#include <trikKernel/deinitializationHelper.h>

#include <gtest/gtest.h>

namespace tests {

/// Test suite for communicator.
class TrikNetworkTests : public testing::Test
{

protected:
	void SetUp() override;
	void TearDown() override;
	void addWorker(QThread *thread);
	trikNetwork::MailboxInterface *mailboxInterface();
	trikNetwork::MailboxInterface *prepareHost(int port, int hullNumber, int portToConnect);
	void cleanUp();
private:
	QList<QThread *> mWorkers;
	QScopedPointer<trikControl::BrickInterface> mBrick;
	QScopedPointer<trikNetwork::MailboxInterface> mMailboxInterface;
};

}

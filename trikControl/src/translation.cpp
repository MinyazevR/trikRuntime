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

#include "translation.h"

#include <QtCore/QProcess>

#include <QsLog.h>

namespace trikControl {

Translation::StreamerProcess::~StreamerProcess()
{
	stop();
}

bool Translation::StreamerProcess::start(const QString &script, const QString &port, const QString &device)
{
	mProcess.reset(new QProcess());
	mProcess->setProgram(script);
	mProcess->setArguments({QStringLiteral("start"), port, device});
	mProcess->start();
	return mProcess->waitForStarted();
}

void Translation::StreamerProcess::stop()
{
	if (!mProcess)
		return;

	if (mProcess->state() != QProcess::NotRunning) {
		// terminate() sends SIGTERM and waitForFinished() blocks on waitpid()
		// until the process actually exits - no busy-waiting. mjpg_streamer's
		// signal handler closes the V4L2 device, so the camera is released only
		// after this returns. Escalate to SIGKILL if it misbehaves.
		mProcess->terminate();
		if (!mProcess->waitForFinished(5000)) {
			QLOG_WARN() << "Brick: mjpg-streamer did not exit after SIGTERM, killing";
			mProcess->kill();
			mProcess->waitForFinished(1000);
		}
	}

	mProcess.reset();
}

} // namespace trikControl

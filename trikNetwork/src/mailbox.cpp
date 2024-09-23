/* Copyright 2014 - 2021 CyberTech Labs Ltd.
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

#include "mailbox.h"
#include "mailboxServer.h"

#include <QtCore/QEventLoop>

#include <trikKernel/configurer.h>
#include <trikKernel/exceptions/malformedConfigException.h>

using namespace trikNetwork;

Mailbox::Mailbox(int port)
{
	init(port);
}

Mailbox::Mailbox(const trikKernel::Configurer &configurer)
{
	bool ok = false;
	const int port = configurer.attributeByDevice("mailbox", "port").toInt(&ok);
	if (!ok) {
		throw trikKernel::MalformedConfigException("Incorrect mailbox port");
	}

	init(port);
}

Mailbox::~Mailbox()
{
	if (mWorkerThread->isRunning()) {
		mWorkerThread->quit();
		mWorkerThread->wait();
	}
}

void Mailbox::joinNetwork(const QString &ip, int port, int hullNumber)
{
	QMetaObject::invokeMethod(mWorker, [=](){mWorker->joinNetwork(ip, port, hullNumber);});
}

bool Mailbox::isConnected() const
{
	bool res;
	QMetaObject::invokeMethod(mWorker, [this, &res](){res = mWorker->isConnected();}
							, Qt::BlockingQueuedConnection);
	return res;
}

void Mailbox::setHullNumber(int hullNumber)
{
	if (isEnabled()) {
		QMetaObject::invokeMethod(mWorker, [this, hullNumber](){mWorker->setHullNumber(hullNumber);});
	}
}

int Mailbox::myHullNumber() const
{
	int res;
	QMetaObject::invokeMethod(mWorker, [this, &res](){res = mWorker->hullNumber();}
							, Qt::BlockingQueuedConnection);
	return res;
}

bool Mailbox::hasServer() const
{
	bool res;
	QMetaObject::invokeMethod(mWorker, [this, &res](){res = mWorker->hasServer();}
							, Qt::BlockingQueuedConnection);
	return res;
}

void Mailbox::renewIp()
{
	QMetaObject::invokeMethod(mWorker, &MailboxServer::renewIp);
}

QString Mailbox::serverIp() const
{
	QHostAddress res;
	QMetaObject::invokeMethod(mWorker, [this, &res](){res = mWorker->serverIp();}
							, Qt::BlockingQueuedConnection);
	return res.toString();
}

QString Mailbox::myIp() const
{
	QHostAddress res;
	QMetaObject::invokeMethod(mWorker, [this, &res](){res = mWorker->myIp();}, Qt::BlockingQueuedConnection);
	return res.toString();
}

void Mailbox::clearQueue()
{
	while (!receive(false).isNull()){
		/// If no messages in queue, receive returns just QString()
	}
}

void Mailbox::stopWaiting()
{
	emit stopWaitingSignal();
}

bool Mailbox::isEnabled()
{
	bool res;
	QMetaObject::invokeMethod(mWorker, [this, &res](){res = !(mWorker == nullptr);}
							, Qt::BlockingQueuedConnection);
	return res;
}

void Mailbox::connect(const QString &ip, int port)
{
	QMetaObject::invokeMethod(mWorker, [=](){mWorker->connectTo(ip, port);});
}

void Mailbox::connect(const QString &ip)
{
	QMetaObject::invokeMethod(mWorker, [=](){mWorker->connectTo(ip);});
}

void Mailbox::send(int hullNumber, const QString &message)
{
	QMetaObject::invokeMethod(mWorker, [=](){mWorker->send(hullNumber, message);});
}

void Mailbox::send(const QString &message)
{
	QMetaObject::invokeMethod(mWorker, [=](){mWorker->send(message);});
}

bool Mailbox::hasMessages()
{
	bool res;
	QMetaObject::invokeMethod(mWorker, [this, &res](){res = mWorker->hasMessages();}
							, Qt::BlockingQueuedConnection);
	return res;
}

QString Mailbox::receive(bool wait)
{
	QString result;

	if (wait && !hasMessages()) {
		QEventLoop loop;
		QObject::connect(this, &Mailbox::stopWaitingSignal, &loop, &QEventLoop::quit, Qt::QueuedConnection);
		loop.exec();
	}

	if (hasMessages()) {
		QMetaObject::invokeMethod(mWorker, [this, &result](){result = mWorker->receive();}
							, Qt::BlockingQueuedConnection);
	}

	return result;
}

void Mailbox::init(int port)
{
	mWorkerThread = new QThread();
	mWorker = new MailboxServer(port);
	mWorker->moveToThread(mWorkerThread);
	QObject::connect(mWorkerThread, &QThread::started, mWorker, &MailboxServer::start);
	QObject::connect(mWorkerThread, &QThread::finished, mWorker, &MailboxServer::deleteLater);
	QObject::connect(mWorkerThread, &QThread::finished, mWorkerThread, &QThread::deleteLater);
	QObject::connect(mWorker, &MailboxServer::newMessage, this, &Mailbox::newMessage);
	QObject::connect(mWorker, &MailboxServer::newMessage, this, &Mailbox::stopWaitingSignal);
	QObject::connect(mWorker, &MailboxServer::connected, this, &Mailbox::updateConnectionStatus);
	QObject::connect(mWorker, &MailboxServer::disconnected, this, &Mailbox::updateConnectionStatus);

	QLOG_INFO() << "Starting Mailbox worker thread" << mWorkerThread;

	mWorkerThread->setObjectName(mWorker->metaObject()->className());
	mWorkerThread->start();
}

void Mailbox::updateConnectionStatus()
{
	int activeConnections;
	QMetaObject::invokeMethod(mWorker, [this, &activeConnections](){
							activeConnections = mWorker->activeConnections();}, Qt::BlockingQueuedConnection);
	emit connectionStatusChanged(activeConnections > 0);
}

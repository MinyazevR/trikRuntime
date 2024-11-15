/* Copyright 2018 Iakov Kirilenko, CyberTech Labs Ltd.
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

#include <QProcess>
#include <QsLog.h>
#include <QVector>
#include <QCoreApplication>

#include <trikNetwork/mailboxInterface.h>
#include <trikKernel/paths.h>
#include <trikKernel/exceptions/internalErrorException.h>
#include <trikScriptRunnerInterface.h>

#include "pythonEngineWorker.h"
#include <Python.h>
#include <PythonQtConversion.h>
#include <PythonQt_QtAll.h>

#ifdef __linux__
#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <string.h>
#endif

void PythonQt_init_QtPyTrikControl(PyObject* module);

using namespace trikScriptRunner;

QAtomicInt PythonEngineWorker::initCounter = 0;

static int quitFromPython(void*) {
	PyErr_SetInterrupt();
	return 0;
}

static void abortPythonInterpreter() {
	if(!Py_IsInitialized()) {
		return;
	}
	PythonQtGILScope _;
	Py_AddPendingCall(&quitFromPython, nullptr);
}

#ifdef __linux__
int find_library_callback(struct dl_phdr_info *info, size_t size, void *data) {
	qDebug() << "I AM IN FIND LIBRARY";
	Q_UNUSED(size);
	auto *library_name = (const char *)data;


	qDebug() << info->dlpi_addr;
	qDebug() << info->dlpi_adds;
	qDebug() << info->dlpi_name;
	qDebug() << info->dlpi_phdr;
	qDebug() << info->dlpi_phnum;
	qDebug() << library_name;
	qDebug() << strstr(info->dlpi_name, library_name);
	if (info->dlpi_name && strstr(info->dlpi_name, library_name)) {
		if (dlopen(info->dlpi_name, RTLD_NOLOAD | RTLD_GLOBAL) == NULL) {
			QLOG_FATAL() << "Library: " << info->dlpi_name << "not load";
			qDebug() << "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
		}
		qDebug() << "OPEN" << info->dlpi_name;
		return 1;
	}
	qDebug() << "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF" << info->dlpi_name;
	QLOG_FATAL() << "Library: " << library_name << "not foud";
	return 0;
}
#endif

PythonEngineWorker::PythonEngineWorker(trikControl::BrickInterface *brick
		, trikNetwork::MailboxInterface * const mailbox
		, TrikScriptControlInterface *scriptControl
		)
	: mBrick(brick)
	, mScriptExecutionControl(scriptControl)
	, mMailbox(mailbox)
	, mWorkingDirectory(trikKernel::Paths::userScriptsPath())
{
	mWaitForInitSemaphore.acquire(1);
}

PythonEngineWorker::~PythonEngineWorker()
{
	if(thread() != QThread::currentThread()) {
		QLOG_FATAL() << "~PythonEngineWorker threading issue";
	}

	stopScript();
	{
		// In python at least before 3.7 (3.5,3.6)
		// we need to make all pending calls before the context
		// is destroyed, otherwise python crashes
		PythonQtGILScope _;
		Py_MakePendingCalls();
		releaseContext();
		mMainContext = nullptr;
		if (mPyInterpreter) {
			Py_EndInterpreter(mPyInterpreter);
			mPyInterpreter = nullptr;
		}
	}

	if (--initCounter == 0) {
		PythonQt::preCleanup();
#if PY_MAJOR_VERSION != 3
#error "Unsupported PYTHON version"
#endif
#if PY_MINOR_VERSION < 6
		Py_Finalize();
#else
		if (Py_FinalizeEx()) {
			QLOG_ERROR() << "Failed to finalize python engine";
		}
#endif
		if (PythonQt::self()) {
			PythonQt::cleanup();
		}

		PyMem_RawFree(mProgramName);
		PyMem_RawFree(mPythonPath);
	}
}

void PythonEngineWorker::init()
{
	qDebug() << __PRETTY_FUNCTION__ << __LINE__;
	if (initCounter++ == 0) {

		QLOG_INFO() << "Built with python:" << PY_VERSION << QString::number(PY_VERSION_HEX, 16);
		QLOG_INFO() << "Running with python:" << Py_GetVersion();
		if (strncmp(PY_VERSION, Py_GetVersion(), 4)) {
			auto const &e = QString("Incompatible Python runtime detected. Expecting version %1, but found %2")
							.arg(PY_VERSION).arg(Py_GetVersion());
			throw trikKernel::InternalErrorException(e);
		}
		constexpr auto varName = "TRIK_PYTHONPATH";
		auto const &path = QProcessEnvironment::systemEnvironment().value(varName);
		if (path.isEmpty()) {
			auto const &e = QString("%1 must be set to correct value").arg(varName);
			QLOG_FATAL() << e;
			throw trikKernel::InternalErrorException(e);
		} else {
			QLOG_INFO() << varName << ":" << path;
		}
#if PY_MINOR_VERSION >= 8
		PyPreConfig pyPreconfig;
		PyPreConfig_InitPythonConfig(&pyPreconfig);

		pyPreconfig.utf8_mode = 1; /// Force UTF-8

		if (PyStatus_Exception(Py_PreInitialize(&pyPreconfig))) {
			auto const *e = "Failed to pre-initialize the Python engine";
			QLOG_FATAL() << e;
			throw trikKernel::InternalErrorException(e);
		}

		/// NB! Py_DecodeLocale requires a pre-initialized Python engine
#endif
		/// TODO: Must point to local .zip file
	qDebug() << __PRETTY_FUNCTION__ << __LINE__;
		mPythonPath = Py_DecodeLocale(path.toStdString().data(), nullptr);
		Py_SetPath(mPythonPath);
qDebug() << __PRETTY_FUNCTION__ << __LINE__;
		mProgramName = Py_DecodeLocale("trikPythonRuntime", nullptr);
		Py_SetProgramName(mProgramName);
qDebug() << __PRETTY_FUNCTION__ << __LINE__;
		if (!qgetenv("TRIK_PYTHON_DEBUG").isEmpty()) {
			Py_VerboseFlag = 3;
			Py_InspectFlag = 1;
			Py_DebugFlag = 2;
		}

		Py_IsolatedFlag = 1;
		Py_BytesWarningFlag = 3;
		Py_DontWriteBytecodeFlag = 1;
//		Py_NoSiteFlag = 1;
//		Py_NoUserSiteDirectory = 1;
#if PY_MAJOR_VERSION != 3
#error "Unsupported PYTHON version"
#else
#if PY_MINOR_VERSION < 6
		Py_Initialize();
#else
		Py_InitializeEx(true); // Initialize handlers, because we need PyErr_SetInterrupt()
#endif

#if PY_MINOR_VERSION < 7
		PyEval_InitThreads();
#endif
#endif
		qDebug() << __PRETTY_FUNCTION__ << __LINE__;
		constexpr auto extractVersionCommand = "(sys.version_info.major,sys.version_info.minor)";
		PythonQtObjectPtr dict;
		dict.setNewRef(PyDict_New());
		PyMapping_SetItemString(dict.object(), "sys", PyImport_ImportModule("sys"));
		PythonQtObjectPtr ver;
		ver.setNewRef(PyRun_StringFlags(extractVersionCommand, Py_eval_input, dict.object(), dict.object(), nullptr));
		if (ver) {
			auto major = PyLong_AsLong(PyTuple_GetItem(ver, 0));
			auto minor = PyLong_AsLong(PyTuple_GetItem(ver, 1));
			if (major != PY_MAJOR_VERSION || minor != PY_MINOR_VERSION) {
				auto const &e = QString("Incompatible Python library detected. Expecting version %1, but found %2.%3")
								.arg(PY_VERSION).arg(major).arg(minor);
				throw trikKernel::InternalErrorException(e);
			}
		} else {
			auto e = QString("Failed to extract Python version from provided library, check %1").arg(varName);
			QLOG_FATAL() << e;
			throw trikKernel::InternalErrorException(e);
		}
		qDebug() << __PRETTY_FUNCTION__ << __LINE__;
	}
	if (!mPyInterpreter) {
		qDebug() << __PRETTY_FUNCTION__ << __LINE__;
//		mPyInterpreter = Py_NewInterpreter();
	}

	qDebug() << __PRETTY_FUNCTION__ << __LINE__;
	if (!PythonQt::self()) {
		PythonQt::setEnableThreadSupport(true);
		PythonQtGILScope _;
		qDebug() << __PRETTY_FUNCTION__ << __LINE__;
		PythonQt::init(PythonQt::RedirectStdOut | PythonQt::PythonAlreadyInitialized, "TRIK_PQT");
		connect(PythonQt::self(), &PythonQt::pythonStdErr, this, &PythonEngineWorker::updateErrorMessage);
		connect(PythonQt::self(), &PythonQt::pythonStdOut, this, [this](const QString& str){
			QTimer::singleShot(0, this, [this, str](){ Q_EMIT this->textInStdOut(str);});
			mScriptExecutionControl->wait(0);
		});
		qDebug() << __PRETTY_FUNCTION__ << __LINE__;
		PythonQtRegisterListTemplateConverter(QVector, uint8_t)
		PythonQt_QtAll::init();
		PythonQt_init_QtPyTrikControl(mMainContext);
	}
	if (!mMainContext) {
		qDebug() << __PRETTY_FUNCTION__ << __LINE__;
		mMainContext = PythonQt::self()->getMainModule();
		recreateContext();
	}
	QLOG_INFO() << "PythonEngineWorker inited";
qDebug() << __PRETTY_FUNCTION__ << __LINE__;
	mWaitForInitSemaphore.release(1);

#if PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION >= 8 && defined(__linux__)
	const char *library_name = "libpython";
	if (!dl_iterate_phdr(find_library_callback, (void *)library_name)) {
		QLOG_FATAL() << "";
	}
#endif
}

bool PythonEngineWorker::recreateContext()
{
	{
		PythonQtGILScope _;
		Py_MakePendingCalls();
		PyErr_CheckSignals();
		PyErr_Clear();
	}
	PythonQt::self()->clearError();
	return initTrik();
}

void PythonEngineWorker::releaseContext()
{
	if (!mMainContext)
		return;

	mMainContext.evalScript("import sys;"
				"to_delete = [];"
				"_init_m = sys.modules.keys() if '_init_m' not in globals() else _init_m;"
				"to_delete = [x for x in sys.modules.keys() if x not in _init_m];"
				"[sys.modules.pop(x) for x in to_delete];"
				"[delattr(sys.modules[__name__], x) for x in dir() if x[0] != '_' and x != 'sys'];"
				"from gc import collect as gc_collect;"
				"gc_collect();");


}

bool PythonEngineWorker::importTrikPy()
{
	const QString systemPyPath = trikKernel::Paths::systemScriptsPath() + "TRIK.py";

	if (!QFileInfo::exists(systemPyPath)) {
		QLOG_ERROR() << "TRIK.py not found, path:" << systemPyPath;
		return false;
	}

	addSearchModuleDirectory(trikKernel::Paths::systemScriptsPath());
	mMainContext.evalScript("from TRIK import *");

	return true;
}

void PythonEngineWorker::addSearchModuleDirectory(const QDir &path)
{
	if (path.path().indexOf("'") != -1)
		return;

	mMainContext.evalScript("import sys; (lambda x: sys.path.append(x) if not x in sys.path else None)('"
							+ path.path() + "')");
}

bool PythonEngineWorker::initTrik()
{
	mMainContext.addObject("_trik_brick_cpp", mBrick);
	mMainContext.addObject("_trik_script_cpp", mScriptExecutionControl);
	mMainContext.addObject("_trik_mailbox_cpp", mMailbox);
	mMainContext.evalScript("import builtins;"
				"builtins._trik_brick_cpp = _trik_brick_cpp;"
				"builtins._trik_script_cpp = _trik_script_cpp;"
				"builtins._trik_mailbox_cpp = _trik_mailbox_cpp;");

	return importTrikPy();
}

void PythonEngineWorker::resetBrick()
{
	QLOG_INFO() << "Stopping robot";

	if (mMailbox) {
		mMailbox->stopWaiting();
		mMailbox->clearQueue();
	}

	mBrick->reset();
}

void PythonEngineWorker::brickBeep()
{
	mBrick->playTone(2500, 150);
}

void PythonEngineWorker::stopScript()
{
	QMutexLocker locker(&mScriptStateMutex);

	if (mState == stopping) {
		// Already stopping, so we can do nothing.
		return;
	}

	if (mState == ready) {
		// Engine is ready for execution.
		return;
	}

	QLOG_INFO() << "PythonEngineWorker: stopping script";

	mState = stopping;

	if (QThread::currentThread() != thread()) {
		abortPythonInterpreter();
	} else {
		QLOG_FATAL() << "Attempt to abort Python from main thread.";
	}

	if (mMailbox) {
		mMailbox->stopWaiting();
	}

	mScriptExecutionControl->reset();

	mState = ready;

	/// @todo: is it actually stopped?

	QLOG_INFO() << "PythonEngineWorker: stopping complete";
}

QStringList PythonEngineWorker::knownNames() const
{
	QSet<QString> result = {"brick", "script", "threading"};
	TrikScriptRunnerInterface::Helper::collectMethodNames(result, &trikControl::BrickInterface::staticMetaObject);
/// TODO:	TrikScriptRunnerInterface::Helper::collectMethodNames(result, mScriptControl.metaObject());
	if (mMailbox) {
		result.insert("mailbox");
		TrikScriptRunnerInterface::Helper::collectMethodNames(result, mMailbox->metaObject());
	}
/// TODO:	TrikScriptRunnerInterface::Helper::collectMethodNames(result, mThreading.metaObject());
	return result.toList();
}

void PythonEngineWorker::setWorkingDirectory(const QDir &workingDir)
{
	mWorkingDirectory = workingDir;
}

void PythonEngineWorker::waitUntilInited()
{
	mWaitForInitSemaphore.acquire(1);
	mWaitForInitSemaphore.release(1);
}

void PythonEngineWorker::run(const QString &script, const QFileInfo &scriptFile)
{
	qDebug() << __PRETTY_FUNCTION__ << __LINE__;
	QMutexLocker locker(&mScriptStateMutex);
	mState = starting;
	QMetaObject::invokeMethod(this, [this, script, scriptFile](){this->doRun(script, scriptFile);});
	qDebug() << "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH";
}

void PythonEngineWorker::doRun(const QString &script, const QFileInfo &scriptFile)
{
	qDebug() << __PRETTY_FUNCTION__ << __LINE__;
	qDebug() << script;
	qDebug() << QThread::currentThread();
	emit startedScript("", 0);
	mErrorMessage.clear();
	/// When starting script execution (by any means), clear button states.
	mBrick->keys()->reset();
	mState = running;
	auto ok = recreateContext();
	QCoreApplication::processEvents();
	if (!ok) {
		emit completed(mErrorMessage,0);
		return;
	}

	addSearchModuleDirectory(mWorkingDirectory.canonicalPath());
	if (scriptFile.isFile()) {
		addSearchModuleDirectory(scriptFile.canonicalPath());
	}

	mMainContext.evalScript(script);

	QLOG_INFO() << "PythonEngineWorker: evaluation ended";

	auto wasError = mState != ready && PythonQt::self()->hadError();
	mState = ready;
	QCoreApplication::processEvents(); //dispatch events before reset
	mScriptExecutionControl->reset();
	releaseContext();
	QCoreApplication::processEvents(); //dispatch events before emitting the signal
	if (wasError) {
		emit completed(mErrorMessage, 0);
	} else {
		emit completed("", 0);
	}
}

void PythonEngineWorker::runDirect(const QString &command)
{
	QMutexLocker locker(&mScriptStateMutex);
	QMetaObject::invokeMethod(this, [this, &command](){doRunDirect(command);});
	qDebug() << "KKKKKKKKKKKKKKKKKKKKKKKKKKKKK";
}

void PythonEngineWorker::doRunDirect(const QString &command)
{
	emit startedDirectScript(0);
	if (PythonQt::self()->hadError()) {
		PythonQt::self()->clearError();
		mErrorMessage.clear();
		recreateContext();
	}
	mMainContext.evalScript(command);
	QCoreApplication::processEvents();
	auto wasError = PythonQt::self()->hadError();
	if (wasError) {
		emit completed(mErrorMessage, 0);
	} else {
		emit completed("", 0);
	}
}

void PythonEngineWorker::updateErrorMessage(const QString &err)
{
	mErrorMessage += err;
}

void PythonEngineWorker::onScriptRequestingToQuit()
{
	throw std::logic_error("Not implemented");
}

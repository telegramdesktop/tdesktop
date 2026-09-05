/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "python_plugins/python_bridge.h"

#include "history/history.h"
#include "history/history_item.h"
#include "data/data_peer.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>

namespace PythonPlugins {
namespace {

Bridge *GlobalBridge = nullptr;

[[nodiscard]] qint64 PeerValue(PeerId id) {
	return static_cast<qint64>(id.value);
}

} // namespace

Bridge::Bridge(QObject *parent) : QObject(parent) {
	Expects(GlobalBridge == nullptr);

	GlobalBridge = this;
}

Bridge::~Bridge() {
	stop();
	if (GlobalBridge == this) {
		GlobalBridge = nullptr;
	}
}

Bridge *Bridge::Instance() {
	return GlobalBridge;
}

void Bridge::start() {
	if (_started) {
		return;
	}
	_started = true;

	const auto host = hostPath();
	if (!QFileInfo::exists(host)) {
		log(u"Python Plugins: host not found: %1"_q.arg(host));
		return;
	}

	for (const auto &candidate : pythonCandidates()) {
		if (candidate == u"python.exe"_q || candidate == u"py.exe"_q) {
			startWithProgram(candidate);
		} else if (QFileInfo::exists(candidate)) {
			startWithProgram(candidate);
		}
		if (_process && _process->state() != QProcess::NotRunning) {
			return;
		}
	}
	log(u"Python Plugins: Python runtime not found."_q);
}

void Bridge::stop() {
	if (!_process) {
		return;
	}
	log(u"Python Plugins: host stopped"_q);
	_process->closeWriteChannel();
	_process->terminate();
	if (!_process->waitForFinished(1000)) {
		_process->kill();
		_process->waitForFinished(1000);
	}
	_process = nullptr;
}

bool Bridge::shouldHide(const HistoryItem *item) const {
	if (!item) {
		return false;
	}
	const auto from = item->from();
	if (_rules.hideBlocked && from->isBlocked()) {
		return true;
	}
	return _rules.hiddenPeers.contains(from->id);
}

void Bridge::sendMessageEvent(const HistoryItem *item) {
	if (!_process || _process->state() != QProcess::Running || !item) {
		return;
	}
	const auto from = item->from();
	const auto history = item->history();

	auto sender = QJsonObject();
	sender.insert(u"id"_q, PeerValue(from->id));
	sender.insert(u"is_blocked"_q, from->isBlocked());

	auto chat = QJsonObject();
	chat.insert(u"id"_q, PeerValue(history->peer->id));

	auto data = QJsonObject();
	data.insert(u"message_id"_q, qint64(item->id.bare));
	data.insert(u"id"_q, qint64(item->id.bare));
	data.insert(u"chat_id"_q, PeerValue(history->peer->id));
	data.insert(u"sender_id"_q, PeerValue(from->id));
	data.insert(u"text"_q, item->originalText().text);
	data.insert(u"sender_is_blocked"_q, from->isBlocked());
	data.insert(u"sender"_q, sender);
	data.insert(u"chat"_q, chat);
	data.insert(u"date"_q, qint64(item->date()));
	data.insert(u"is_service"_q, item->isService());

	auto event = QJsonObject();
	event.insert(u"type"_q, u"event"_q);
	event.insert(u"event"_q, u"message"_q);
	event.insert(u"data"_q, data);
	writeJson(event);
}

rpl::producer<> Bridge::rulesChanged() const {
	return _rulesChanged.events();
}

void Bridge::startWithProgram(const QString &program) {
	log(u"Python Plugins: starting host"_q);
	const auto host = hostPath();
	auto process = std::make_unique<QProcess>(this);
	process->setProgram(program);
	process->setArguments({ host });
	process->setWorkingDirectory(QFileInfo(host).absolutePath());
	process->setProcessChannelMode(QProcess::SeparateChannels);
	QObject::connect(process.get(), &QProcess::readyReadStandardOutput, [=] {
		readStdout();
	});
	QObject::connect(process.get(), &QProcess::readyReadStandardError, [=] {
		readStderr();
	});
	QObject::connect(process.get(), &QProcess::errorOccurred, [=](QProcess::ProcessError error) {
		log(u"Python Plugins: host error: %1"_q.arg(int(error)));
	});
	QObject::connect(process.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=](
			int code,
			QProcess::ExitStatus status) {
		log(u"Python Plugins: host exited: code=%1 status=%2"_q
			.arg(code)
			.arg(int(status)));
	});
	process->start();
	if (!process->waitForStarted(500)) {
		log(u"Python Plugins: host error: %1"_q.arg(process->errorString()));
		return;
	}
	_process = std::move(process);
	log(u"Python Plugins: host started"_q);
}

void Bridge::readStdout() {
	_stdoutBuffer += _process->readAllStandardOutput();
	while (true) {
		const auto newline = _stdoutBuffer.indexOf('\n');
		if (newline < 0) {
			break;
		}
		const auto line = _stdoutBuffer.left(newline).trimmed();
		_stdoutBuffer.remove(0, newline + 1);
		if (!line.isEmpty()) {
			processLine(line);
		}
	}
}

void Bridge::readStderr() {
	const auto text = QString::fromUtf8(_process->readAllStandardError()).trimmed();
	if (!text.isEmpty()) {
		log(u"Python Plugins: host error: %1"_q.arg(text));
	}
}

void Bridge::processLine(const QByteArray &line) {
	const auto document = QJsonDocument::fromJson(line);
	if (!document.isObject()) {
		log(u"Python Plugins: host output: %1"_q.arg(QString::fromUtf8(line)));
		return;
	}
	const auto object = document.object();
	if (object.value(u"type"_q).toString() == u"command"_q) {
		processCommand(object);
	}
}

void Bridge::processCommand(const QJsonObject &object) {
	const auto command = object.value(u"command"_q).toString();
	if (command == u"set_rule"_q) {
		const auto name = object.value(u"name"_q).toString();
		if (name == u"hide_blocked"_q) {
			_rules.hideBlocked = object.value(u"value"_q).toBool();
			log(u"Python Plugins: rule hide_blocked=%1"_q.arg(_rules.hideBlocked));
			_rulesChanged.fire({});
		}
	} else if (command == u"hide_user"_q) {
		_rules.hiddenPeers.emplace(PeerId(object.value(u"user_id"_q).toInteger()));
		_rulesChanged.fire({});
	} else if (command == u"show_user"_q) {
		_rules.hiddenPeers.remove(PeerId(object.value(u"user_id"_q).toInteger()));
		_rulesChanged.fire({});
	} else if (command == u"clear_hidden_users"_q) {
		_rules.hiddenPeers.clear();
		_rulesChanged.fire({});
	} else if (command == u"log"_q) {
		log(u"Python Plugins: %1"_q.arg(object.value(u"message"_q).toString()));
	}
}

void Bridge::writeJson(const QJsonObject &object) {
	if (!_process || _process->state() != QProcess::Running) {
		return;
	}
	_process->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	_process->write("\n");
}

QString Bridge::hostPath() const {
	return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
		u"python_plugins/_tdesktop_host.py"_q);
}

QStringList Bridge::pythonCandidates() const {
	const auto dir = QCoreApplication::applicationDirPath();
	return {
		QDir(dir).absoluteFilePath(u"python/python.exe"_q),
		QDir(dir).absoluteFilePath(u"python.exe"_q),
		u"python.exe"_q,
		u"py.exe"_q,
	};
}

void Bridge::log(const QString &message) const {
	LOG((message));
}

} // namespace PythonPlugins

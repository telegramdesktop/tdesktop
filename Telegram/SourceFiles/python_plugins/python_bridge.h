/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "data/data_peer_id.h"
#include "rpl/event_stream.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

class HistoryItem;
class QProcess;

namespace PythonPlugins {

struct VisibilityRules {
	bool hideBlocked = false;
	base::flat_set<PeerId> hiddenPeers;
};

class Bridge final : public QObject {
public:
	explicit Bridge(QObject *parent = nullptr);
	~Bridge();

	void start();
	void stop();

	[[nodiscard]] bool shouldHide(const HistoryItem *item) const;
	void sendMessageEvent(const HistoryItem *item);
	[[nodiscard]] rpl::producer<> rulesChanged() const;

	[[nodiscard]] const VisibilityRules &rules() const {
		return _rules;
	}

	static Bridge *Instance();

private:
	void startWithProgram(const QString &program);
	void readStdout();
	void readStderr();
	void processLine(const QByteArray &line);
	void processCommand(const QJsonObject &object);
	void writeJson(const QJsonObject &object);
	[[nodiscard]] QString hostPath() const;
	[[nodiscard]] QStringList pythonCandidates() const;
	void log(const QString &message) const;

	std::unique_ptr<QProcess> _process;
	VisibilityRules _rules;
	QByteArray _stdoutBuffer;
	rpl::event_stream<> _rulesChanged;
	bool _started = false;
};

} // namespace PythonPlugins

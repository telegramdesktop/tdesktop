/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/binary_guard.h"

class AuthSession;

namespace Support {
namespace details {

struct TemplatesQuestion {
	QString question;
	QStringList originalKeys;
	QStringList normalizedKeys;
	QString value;
};

struct TemplatesFile {
	QString url;
	std::map<QString, TemplatesQuestion> questions;
};

struct TemplatesData {
	std::map<QString, TemplatesFile> files;
};

struct TemplatesIndex {
	using Id = std::pair<QString, QString>; // filename, normalized question
	using Term = std::pair<QString, int>; // search term, weight

	std::map<QChar, std::vector<Id>> first;
	std::map<Id, std::vector<Term>> full;
};

} // namespace details

class Templates : public base::has_weak_ptr {
public:
	explicit Templates(not_null<AuthSession*> session);

	void reload();

	using Question = details::TemplatesQuestion;
	std::vector<Question> query(const QString &text) const;

	auto errors() const {
		return _errors.events();
	}

	struct QuestionByKey {
		Question question;
		QString key;
	};
	std::optional<QuestionByKey> matchExact(QString text) const;
	std::optional<QuestionByKey> matchFromEnd(QString text) const;
	int maxKeyLength() const {
		return _maxKeyLength;
	}

	~Templates();

private:
	struct Updates;

	void load();
	void update();
	void ensureUpdatesCreated();
	void updateRequestFinished(QNetworkReply *reply);
	void checkUpdateFinished();
	void setData(details::TemplatesData &&data);

	not_null<AuthSession*> _session;

	details::TemplatesData _data;
	details::TemplatesIndex _index;
	rpl::event_stream<QStringList> _errors;
	base::binary_guard _reading;
	bool _reloadAfterRead = false;
	rpl::lifetime _reloadToastSubscription;

	int _maxKeyLength = 0;

	std::unique_ptr<Updates> _updates;

	rpl::lifetime _lifetime;

};

} // namespace Support

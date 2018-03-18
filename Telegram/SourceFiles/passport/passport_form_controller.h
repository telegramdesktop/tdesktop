/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

namespace Window {
class Controller;
} // namespace Window

namespace Passport {

struct FormRequest {
	FormRequest(
		UserId botId,
		const QStringList &scope,
		const QString &callbackUrl,
		const QString &publicKey);

	UserId botId;
	QStringList scope;
	QString callbackUrl;
	QString publicKey;

};

class FormController : private MTP::Sender {
public:
	FormController(
		not_null<Window::Controller*> controller,
		const FormRequest &request);

	void show();

private:
	struct File {
		uint64 id = 0;
		uint64 accessHash = 0;
		int32 size = 0;
		int32 dcId = 0;
		QByteArray fileHash;
	};
	struct Value {
		QString name;

		QByteArray data;
		QByteArray dataHash;
		QByteArray dataSecret;

		QString text;
		QByteArray textHash;

		std::vector<File> files;
		QByteArray filesHash;
		QByteArray filesSecret;
	};
	struct Field {
		enum class Type {
			Identity,
			Address,
			Phone,
			Email,
		};
		explicit Field(Type type);

		Type type;
		Value data;
		base::optional<Value> document;
	};
	struct Form {
		bool requestWrite = false;
		std::vector<Field> fields;
	};
	Value convertValue(const MTPSecureValue &value) const;

	void requestForm();
	void requestPassword();

	void formDone(const MTPaccount_AuthorizationForm &result);
	void formFail(const RPCError &error);
	void parseForm(const MTPaccount_AuthorizationForm &result);
	void showForm();

	void passwordDone(const MTPaccount_Password &result);
	void passwordFail(const RPCError &error);
	void createPassword(const MTPDaccount_noPassword &settings);
	void checkPassword(const MTPDaccount_password &settings);

	not_null<Window::Controller*> _controller;
	FormRequest _request;
	UserData *_bot = nullptr;
	QString _origin;

	Form _form;

};

} // namespace Passport

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "passport/passport_form_controller.h"

namespace Passport {

struct Scope {
	enum class Type {
		Identity,
		Address,
		Phone,
		Email,
	};
	Scope(Type type, not_null<const Value*> fields);

	Type type;
	not_null<const Value*> fields;
	std::vector<not_null<const Value*>> documents;
	bool selfieRequired = false;
};

struct ScopeRow {
	QString title;
	QString description;
	QString ready;
};

std::vector<Scope> ComputeScopes(
	not_null<const FormController*> controller);
QString ComputeScopeRowReadyString(const Scope &scope);
ScopeRow ComputeScopeRow(const Scope &scope);

class ViewController {
public:
	virtual void showAskPassword() = 0;
	virtual void showNoPassword() = 0;
	virtual void showPasswordUnconfirmed() = 0;
	virtual void editScope(int index) = 0;

	virtual void showBox(object_ptr<BoxContent> box) = 0;
	virtual void showToast(const QString &text) = 0;

	virtual ~ViewController() {
	}

	template <typename BoxType>
	QPointer<BoxType> show(object_ptr<BoxType> content) {
		auto result = QPointer<BoxType>(content.data());
		showBox(std::move(content));
		return result;
	}

};

} // namespace Passport

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
	std::vector<not_null<const Value*>> files;
	bool selfieRequired = false;
};

std::vector<Scope> ComputeScopes(not_null<FormController*> controller);

class ViewController {
public:
	virtual void showAskPassword() = 0;
	virtual void showNoPassword() = 0;
	virtual void showPasswordUnconfirmed() = 0;
	virtual void editScope(int index) = 0;

	virtual ~ViewController() {
	}

};

} // namespace Passport

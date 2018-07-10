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
	QString error;
};

std::vector<Scope> ComputeScopes(
	not_null<const FormController*> controller);
QString ComputeScopeRowReadyString(const Scope &scope);
ScopeRow ComputeScopeRow(const Scope &scope);

class ViewController {
public:
	virtual void showAskPassword() = 0;
	virtual void showNoPassword() = 0;
	virtual void showCriticalError(const QString &error) = 0;
	virtual void showUpdateAppBox() = 0;
	virtual void editScope(int index) = 0;

	virtual void showBox(
		object_ptr<BoxContent> box,
		LayerOptions options,
		anim::type animated) = 0;
	virtual void showToast(const QString &text) = 0;
	virtual void suggestReset(Fn<void()> callback) = 0;

	virtual int closeGetDuration() = 0;

	virtual ~ViewController() {
	}

	template <typename BoxType>
	QPointer<BoxType> show(
			object_ptr<BoxType> box,
			LayerOptions options = LayerOption::KeepOther,
			anim::type animated = anim::type::normal) {
		auto result = QPointer<BoxType>(box.data());
		showBox(std::move(box), options, animated);
		return result;
	}

};

} // namespace Passport

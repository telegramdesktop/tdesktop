/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "passport/passport_form_controller.h"
#include "base/object_ptr.h"
#include "ui/layers/box_content.h"

namespace Passport {

struct Scope {
	enum class Type {
		PersonalDetails,
		Identity,
		AddressDetails,
		Address,
		Phone,
		Email,
	};
	explicit Scope(Type type);

	Type type;
	const Value *details = nullptr;
	std::vector<not_null<const Value*>> documents;
};

struct ScopeRow {
	QString title;
	QString description;
	QString ready;
	QString error;
};

bool CanHaveErrors(Value::Type type);
bool ValidateForm(const Form &form);
std::vector<Scope> ComputeScopes(const Form &form);
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
		object_ptr<Ui::BoxContent> box,
		Ui::LayerOptions options,
		anim::type animated) = 0;
	virtual void showToast(const QString &text) = 0;
	virtual void suggestReset(Fn<void()> callback) = 0;

	virtual int closeGetDuration() = 0;

	virtual ~ViewController() {
	}

	template <typename BoxType>
	QPointer<BoxType> show(
			object_ptr<BoxType> box,
			Ui::LayerOptions options = Ui::LayerOption::KeepOther,
			anim::type animated = anim::type::normal) {
		auto result = QPointer<BoxType>(box.data());
		showBox(std::move(box), options, animated);
		return result;
	}

};

} // namespace Passport

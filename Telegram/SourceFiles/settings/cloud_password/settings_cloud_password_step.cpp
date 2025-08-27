/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_step.h"

#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/cloud_password/settings_cloud_password_email.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_hint.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_session_controller.h"

namespace Settings::CloudPassword {

AbstractStep::AbstractStep(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractSection(parent)
, _controller(controller) {
}

not_null<Window::SessionController*> AbstractStep::controller() const {
	return _controller;
}

Api::CloudPassword &AbstractStep::cloudPassword() {
	return _controller->session().api().cloudPassword();
}

rpl::producer<AbstractStep::Types> AbstractStep::removeTypes() {
	return rpl::never<Types>();
}

void AbstractStep::showBack() {
	_showBack.fire({});
}

void AbstractStep::showOther(Type type) {
	_showOther.fire_copy(type);
}

void AbstractStep::setFocusCallback(Fn<void()> callback) {
	_setInnerFocusCallback = callback;
}

rpl::producer<> AbstractStep::showFinishes() const {
	return _showFinished.events();
}

void AbstractStep::showFinished() {
	_showFinished.fire({});
}

void AbstractStep::setInnerFocus() {
	if (_setInnerFocusCallback) {
		_setInnerFocusCallback();
	}
}

bool AbstractStep::isPasswordInvalidError(const QString &type) {
	if (type == u"PASSWORD_HASH_INVALID"_q
		|| type == u"SRP_PASSWORD_CHANGED"_q) {

		// Most likely the cloud password has been changed on another device.
		// Quit.
		_quits.fire(AbstractStep::Types{
			CloudPasswordStartId(),
			CloudPasswordInputId(),
			CloudPasswordHintId(),
			CloudPasswordEmailId(),
			CloudPasswordEmailConfirmId(),
			CloudPasswordManageId(),
		});
		controller()->show(
			Ui::MakeInformBox(tr::lng_cloud_password_expired()),
			Ui::LayerOption::CloseOther);
		setStepData(StepData());
		showBack();
		return true;
	}
	return false;
}

rpl::producer<Type> AbstractStep::sectionShowOther() {
	return _showOther.events();
}

rpl::producer<> AbstractStep::sectionShowBack() {
	return _showBack.events();
}

rpl::producer<std::vector<Type>> AbstractStep::removeFromStack() {
	return rpl::merge(removeTypes(), _quits.events());
}

void AbstractStep::setStepDataReference(std::any &data) {
	_stepData = &data;
}

StepData AbstractStep::stepData() const {
	if (!_stepData || !_stepData->has_value()) {
		StepData();
	}
	const auto my = std::any_cast<StepData>(_stepData);
	return my ? (*my) : StepData();
}

void AbstractStep::setStepData(StepData data) {
	if (_stepData) {
		*_stepData = data;
	}
}

AbstractStep::~AbstractStep() = default;

} // namespace Settings::CloudPassword

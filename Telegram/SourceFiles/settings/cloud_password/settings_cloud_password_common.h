/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"

namespace Ui {
class FlatLabel;
class PasswordInput;
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Settings::CloudPassword {

void SetupHeader(
	not_null<Ui::VerticalLayout*> content,
	const QString &lottie,
	rpl::producer<> &&showFinished,
	rpl::producer<QString> &&subtitle,
	rpl::producer<QString> &&about);

[[nodiscard]] not_null<Ui::PasswordInput*> AddPasswordField(
	not_null<Ui::VerticalLayout*> content,
	rpl::producer<QString> &&placeholder,
	const QString &text);

[[nodiscard]] not_null<Ui::FlatLabel*> AddError(
	not_null<Ui::VerticalLayout*> content,
	Ui::PasswordInput *input);

[[nodiscard]] not_null<Ui::RoundButton*> AddDoneButton(
	not_null<Ui::VerticalLayout*> content,
	rpl::producer<QString> &&text);

void AddSkipInsteadOfField(not_null<Ui::VerticalLayout*> content);
void AddSkipInsteadOfError(not_null<Ui::VerticalLayout*> content);

class AbstractStep : public AbstractSection {
public:
	AbstractStep(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~AbstractStep();

	void showFinished() override;
	void setInnerFocus() override;
	[[nodiscard]] rpl::producer<Type> sectionShowOther() override;
	[[nodiscard]] rpl::producer<> sectionShowBack() override;

protected:
	[[nodiscard]] not_null<Window::SessionController*> controller() const;

	void showBack();
	void showOther(Type type);

	void setFocusCallback(Fn<void()> callback);

	[[nodiscard]] rpl::producer<> showFinishes() const;

private:
	const not_null<Window::SessionController*> _controller;

	Fn<void()> _setInnerFocusCallback;

	rpl::event_stream<> _showFinished;
	rpl::event_stream<Type> _showOther;
	rpl::event_stream<> _showBack;

};

template <typename SectionType>
class TypedAbstractStep : public AbstractStep {
public:
	TypedAbstractStep(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: AbstractStep(parent, controller) {
		static_cast<SectionType*>(this)->setupContent();
	}

	[[nodiscard]] static Type Id() {
		return &SectionMetaImplementation<SectionType>::Meta;
	}
	[[nodiscard]] Type id() const final override {
		return Id();
	}

};

} // namespace Settings::CloudPassword


/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Api {
class CloudPassword;
} // namespace Api

namespace Settings::CloudPassword {

struct StepData;

class AbstractStep : public AbstractSection {
public:
	using Types = std::vector<Type>;
	AbstractStep(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~AbstractStep();

	void showFinished() override final;
	void setInnerFocus() override final;
	[[nodiscard]] rpl::producer<Type> sectionShowOther() override final;
	[[nodiscard]] rpl::producer<> sectionShowBack() override final;

	[[nodiscard]] rpl::producer<Types> removeFromStack() override final;

	void setStepDataReference(std::any &data) override;

protected:
	[[nodiscard]] not_null<Window::SessionController*> controller() const;
	[[nodiscard]] Api::CloudPassword &cloudPassword();

	[[nodiscard]] virtual rpl::producer<Types> removeTypes();

	bool isPasswordInvalidError(const QString &type);

	void showBack();
	void showOther(Type type);

	void setFocusCallback(Fn<void()> callback);

	[[nodiscard]] rpl::producer<> showFinishes() const;

	StepData stepData() const;
	void setStepData(StepData data);

private:
	const not_null<Window::SessionController*> _controller;

	Fn<void()> _setInnerFocusCallback;

	rpl::event_stream<> _showFinished;
	rpl::event_stream<Type> _showOther;
	rpl::event_stream<> _showBack;
	rpl::event_stream<Types> _quits;

	std::any *_stepData;

};

template <typename SectionType>
class TypedAbstractStep : public AbstractStep {
public:
	using AbstractStep::AbstractStep;

	void setStepDataReference(std::any &data) override final {
		AbstractStep::setStepDataReference(data);
		static_cast<SectionType*>(this)->setupContent();
	}

	[[nodiscard]] static Type Id() {
		return SectionFactory<SectionType>::Instance();
	}
	[[nodiscard]] Type id() const final override {
		return Id();
	}

};

} // namespace Settings::CloudPassword


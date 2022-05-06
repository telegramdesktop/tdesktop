/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "settings/settings_common.h"
#include "ui/widgets/box_content_divider.h"

namespace Ui {
template <typename Widget>
class CenterWrap;
class FlatLabel;
class InputField;
class PasswordInput;
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Settings::CloudPassword {

struct StepData {
	QString password;
	QString hint;
};

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

[[nodiscard]] not_null<Ui::CenterWrap<Ui::InputField>*> AddWrappedField(
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

struct BottomButton {
	QPointer<Ui::RpWidget> content;
	rpl::producer<bool> isBottomFillerShown;
};

BottomButton CreateBottomDisableButton(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<QRect> &&sectionGeometryValue,
	rpl::producer<QString> &&buttonText,
	Fn<void()> &&callback);

class OneEdgeBoxContentDivider : public Ui::BoxContentDivider {
public:
	using Ui::BoxContentDivider::BoxContentDivider;

	void skipEdge(Qt::Edge edge, bool skip);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Qt::Edges _skipEdges;

};

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

	void setStepDataReference(std::any &data) override;

protected:
	[[nodiscard]] not_null<Window::SessionController*> controller() const;

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
		return &SectionMetaImplementation<SectionType>::Meta;
	}
	[[nodiscard]] Type id() const final override {
		return Id();
	}

};

} // namespace Settings::CloudPassword


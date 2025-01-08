/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_variant.h"
#include "ui/widgets/box_content_divider.h"

namespace Ui {
template <typename Widget>
class CenterWrap;
class FlatLabel;
class InputField;
class LinkButton;
class PasswordInput;
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Api {
class CloudPassword;
} // namespace Api

namespace Settings::CloudPassword {

struct StepData {
	QString currentPassword;
	QString password;
	QString hint;
	QString email;
	int unconfirmedEmailLengthCode;
	bool setOnlyRecoveryEmail = false;

	struct ProcessRecover {
		bool setNewPassword = false;
		QString checkedCode;
		QString emailPattern;
	};
	ProcessRecover processRecover;
};

void SetupAutoCloseTimer(
	rpl::lifetime &lifetime,
	Fn<void()> callback,
	Fn<crl::time()> lastNonIdleTime);

void SetupHeader(
	not_null<Ui::VerticalLayout*> content,
	const QString &lottie,
	rpl::producer<> &&showFinished,
	rpl::producer<QString> &&subtitle,
	v::text::data &&about);

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

[[nodiscard]] not_null<Ui::LinkButton*> AddLinkButton(
	not_null<Ui::CenterWrap<Ui::InputField>*> wrap,
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

} // namespace Settings::CloudPassword


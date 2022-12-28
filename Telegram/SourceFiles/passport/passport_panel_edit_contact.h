/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"

namespace Ui {
class MaskedInputField;
class PlainShadow;
class RoundButton;
class VerticalLayout;
class BoxContent;
} // namespace Ui

namespace Passport {

class PanelController;

struct EditContactScheme {
	enum class ValueType {
		Phone,
		Text,
	};
	explicit EditContactScheme(ValueType type);

	ValueType type;

	QString aboutExisting;
	QString newHeader;
	rpl::producer<QString> newPlaceholder;
	QString aboutNew;
	Fn<bool(const QString &value)> validate;
	Fn<QString(const QString &value)> format;
	Fn<QString(const QString &value)> postprocess;

};

class PanelEditContact : public Ui::RpWidget {
public:
	using Scheme = EditContactScheme;

	PanelEditContact(
		QWidget *parent,
		not_null<PanelController*> controller,
		Scheme scheme,
		const QString &data,
		const QString &existing);

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupControls(
		const QString &data,
		const QString &existing);
	void updateControlsGeometry();

	void save();
	void save(const QString &value);

	not_null<PanelController*> _controller;
	Scheme _scheme;

	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::MaskedInputField> _field;
	object_ptr<Ui::PlainShadow> _bottomShadow;
	object_ptr<Ui::RoundButton> _done;

};

object_ptr<Ui::BoxContent> VerifyPhoneBox(
	const QString &phone,
	int codeLength,
	const QString &openUrl,
	Fn<void(QString code)> submit,
	rpl::producer<QString> call,
	rpl::producer<QString> error);
object_ptr<Ui::BoxContent> VerifyEmailBox(
	const QString &email,
	int codeLength,
	Fn<void(QString code)> submit,
	Fn<void()> resend,
	rpl::producer<QString> error,
	rpl::producer<QString> resent);

} // namespace Passport

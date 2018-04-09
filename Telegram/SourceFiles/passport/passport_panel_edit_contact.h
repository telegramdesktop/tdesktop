/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class InputField;
class PlainShadow;
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Passport {

class PanelController;

class PanelEditContact : public Ui::RpWidget {
public:
	struct Scheme {
		enum class ValueType {
			Phone,
			Text,
		};
		ValueType type = ValueType::Phone;

		QString aboutExisting;
		QString newHeader;
		base::lambda<QString()> newPlaceholder;
		QString aboutNew;
		base::lambda<bool(const QString &value)> validate;
		base::lambda<QString(const QString &value)> preprocess;
		base::lambda<QString(const QString &value)> postprocess;

	};

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
	QPointer<Ui::InputField> _field;
	object_ptr<Ui::PlainShadow> _bottomShadow;
	object_ptr<Ui::RoundButton> _done;

};

object_ptr<BoxContent> VerifyPhoneBox(
	const QString &phone,
	int codeLength,
	base::lambda<void(QString code)> submit,
	rpl::producer<QString> call,
	rpl::producer<QString> error);
object_ptr<BoxContent> VerifyEmailBox(
	const QString &email,
	int codeLength,
	base::lambda<void(QString code)> submit,
	rpl::producer<QString> error);

} // namespace Passport

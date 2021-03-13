/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/input_fields.h"

namespace Ui {

class CountryCodeInput : public MaskedInputField {
	Q_OBJECT

public:
	CountryCodeInput(QWidget *parent, const style::InputField &st);

public Q_SLOTS:
	void startErasing(QKeyEvent *e);
	void codeSelected(const QString &code);

Q_SIGNALS:
	void codeChanged(const QString &code);
	void addedToNumber(const QString &added);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	bool _nosignal;

};

class PhonePartInput : public MaskedInputField {
	Q_OBJECT

public:
	PhonePartInput(QWidget *parent, const style::InputField &st);

public Q_SLOTS:
	void addedToNumber(const QString &added);
	void onChooseCode(const QString &code);

Q_SIGNALS:
	void voidBackspace(QKeyEvent *e);

protected:
	void keyPressEvent(QKeyEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p) override;

private:
	QVector<int> _pattern;
	QString _additionalPlaceholder;

};

class UsernameInput : public MaskedInputField {
public:
	UsernameInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val,
		const QString &linkPlaceholder);

	void setLinkPlaceholder(const QString &placeholder);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p) override;

private:
	QString _linkPlaceholder;

};

class PhoneInput : public MaskedInputField {
public:
	PhoneInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &defaultValue,
		QString value);

	void clearText();

protected:
	void focusInEvent(QFocusEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(Painter &p) override;

private:
	QString _defaultValue;
	QVector<int> _pattern;
	QString _additionalPlaceholder;

};

} // namespace Ui

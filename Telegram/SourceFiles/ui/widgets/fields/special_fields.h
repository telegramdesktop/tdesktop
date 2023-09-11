/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/fields/masked_input_field.h"

namespace Ui {

class CountryCodeInput : public MaskedInputField {
public:
	CountryCodeInput(QWidget *parent, const style::InputField &st);

	void startErasing(QKeyEvent *e);

	[[nodiscard]] rpl::producer<QString> addedToNumber() const {
		return _addedToNumber.events();
	}
	[[nodiscard]] rpl::producer<QString> codeChanged() const {
		return _codeChanged.events();
	}

	void codeSelected(const QString &code);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

private:
	bool _nosignal = false;
	rpl::event_stream<QString> _addedToNumber;
	rpl::event_stream<QString> _codeChanged;

};

class PhonePartInput : public MaskedInputField {
public:
	using GroupsCallback = Fn<QVector<int>(const QString &)>;

	PhonePartInput(
		QWidget *parent,
		const style::InputField &st,
		GroupsCallback groupsCallback);

	[[nodiscard]] auto frontBackspaceEvent() const
	-> rpl::producer<not_null<QKeyEvent*>> {
		return _frontBackspaceEvent.events();
	}

	void addedToNumber(const QString &added);
	void chooseCode(const QString &code);

protected:
	void keyPressEvent(QKeyEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(QPainter &p) override;

private:
	void updatePattern(QVector<int> &&pattern);

	QString _code;
	QString _lastDigits;
	QVector<int> _pattern;
	QString _additionalPlaceholder;
	rpl::event_stream<not_null<QKeyEvent*>> _frontBackspaceEvent;
	GroupsCallback _groupsCallback;

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
	void paintAdditionalPlaceholder(QPainter &p) override;

private:
	QString _linkPlaceholder;

};

class PhoneInput : public MaskedInputField {
public:
	using GroupsCallback = Fn<QVector<int>(const QString &)>;

	PhoneInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &defaultValue,
		QString value,
		GroupsCallback groupsCallback);

	void clearText();

protected:
	void focusInEvent(QFocusEvent *e) override;

	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
	void paintAdditionalPlaceholder(QPainter &p) override;

private:
	QString _defaultValue;
	QVector<int> _pattern;
	QString _additionalPlaceholder;

	GroupsCallback _groupsCallback;

};

} // namespace Ui

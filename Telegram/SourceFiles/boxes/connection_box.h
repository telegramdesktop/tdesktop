/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class InputField;
class PortInput;
class PasswordInput;
class Checkbox;
template <typename Enum>
class RadioenumGroup;
template <typename Enum>
class Radioenum;
} // namespace Ui

class ConnectionBox : public BoxContent {
	Q_OBJECT

public:
	ConnectionBox(QWidget *parent);

	static void ShowApplyProxyConfirmation(const QMap<QString, QString> &fields);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onSubmit();
	void onFieldFocus();
	void onSave();

private:
	void typeChanged(DBIConnectionType type);
	void updateControlsVisibility();
	void updateControlsPosition();
	bool badProxyValue() const;

	object_ptr<Ui::InputField> _hostInput;
	object_ptr<Ui::PortInput> _portInput;
	object_ptr<Ui::InputField> _userInput;
	object_ptr<Ui::PasswordInput> _passwordInput;
	std::shared_ptr<Ui::RadioenumGroup<DBIConnectionType>> _typeGroup;
	object_ptr<Ui::Radioenum<DBIConnectionType>> _autoRadio;
	object_ptr<Ui::Radioenum<DBIConnectionType>> _httpProxyRadio;
	object_ptr<Ui::Radioenum<DBIConnectionType>> _tcpProxyRadio;
	object_ptr<Ui::Checkbox> _tryIPv6;

};

class AutoDownloadBox : public BoxContent {
	Q_OBJECT

public:
	AutoDownloadBox(QWidget *parent);

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private slots:
	void onSave();

private:
	object_ptr<Ui::Checkbox> _photoPrivate;
	object_ptr<Ui::Checkbox> _photoGroups;
	object_ptr<Ui::Checkbox> _audioPrivate;
	object_ptr<Ui::Checkbox> _audioGroups;
	object_ptr<Ui::Checkbox> _gifPrivate;
	object_ptr<Ui::Checkbox> _gifGroups;
	object_ptr<Ui::Checkbox> _gifPlay;

	int _sectionHeight = 0;

};

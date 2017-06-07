/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class FlatLabel;
class LinkButton;
class Checkbox;
} // namespace Ui

class EditParticipantBox : public BoxContent {
public:
	EditParticipantBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user);

protected:
	void resizeToContent();

	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	gsl::not_null<UserData*> user() const {
		return _user;
	}
	gsl::not_null<ChannelData*> channel() const {
		return _channel;
	}

	template <typename Widget>
	QPointer<Widget> addControl(object_ptr<Widget> row) {
		_rows.push_back(std::move(row));
		return static_cast<Widget*>(_rows.back().data());
	}

private:
	gsl::not_null<UserData*> _user;
	gsl::not_null<ChannelData*> _channel;

	std::vector<object_ptr<TWidget>> _rows;

};

class EditAdminBox : public EditParticipantBox {
public:
	EditAdminBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, const MTPChannelAdminRights &rights, base::lambda<void(MTPChannelAdminRights)> callback);

protected:
	void prepare() override;

private:
	using Flag = MTPDchannelAdminRights::Flag;

	void refreshAboutAddAdminsText();

	MTPChannelAdminRights _rights;

	std::map<Flag, QPointer<Ui::Checkbox>> _checkboxes;
	QPointer<Ui::FlatLabel> _aboutAddAdmins;

};

class EditRestrictedBox : public EditParticipantBox {
public:
	EditRestrictedBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, const MTPChannelBannedRights &rights, base::lambda<void(MTPChannelBannedRights)> callback);

protected:
	void prepare() override;

private:
	using Flag = MTPDchannelBannedRights::Flag;

	MTPChannelBannedRights _rights;

	std::map<Flag, QPointer<Ui::Checkbox>> _checkboxes;

};

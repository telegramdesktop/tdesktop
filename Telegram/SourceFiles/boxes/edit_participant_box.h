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

class CalendarBox;

class EditParticipantBox : public BoxContent {
public:
	EditParticipantBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, bool hasAdminRights);

protected:
	void prepare() override;

	void resizeToContent();

	gsl::not_null<UserData*> user() const {
		return _user;
	}
	gsl::not_null<ChannelData*> channel() const {
		return _channel;
	}

	template <typename Widget>
	QPointer<Widget> addControl(object_ptr<Widget> row);

	bool hasAdminRights() const {
		return _hasAdminRights;
	}

private:
	gsl::not_null<ChannelData*> _channel;
	gsl::not_null<UserData*> _user;
	bool _hasAdminRights = false;

	class Inner;
	QPointer<Inner> _inner;

};

class EditAdminBox : public EditParticipantBox {
public:
	EditAdminBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, bool hasAdminRights, const MTPChannelAdminRights &rights, base::lambda<void(MTPChannelAdminRights)> callback);

	static MTPChannelAdminRights DefaultRights(gsl::not_null<ChannelData*> channel);

protected:
	void prepare() override;

private:
	using Flag = MTPDchannelAdminRights::Flag;
	using Flags = MTPDchannelAdminRights::Flags;

	void applyDependencies(QPointer<Ui::Checkbox> changed);
	void refreshAboutAddAdminsText();

	MTPChannelAdminRights _rights;
	std::vector<std::pair<Flag, Flag>> _dependencies;
	base::lambda<void(MTPChannelAdminRights)> _saveCallback;

	std::map<Flags, QPointer<Ui::Checkbox>> _checkboxes;
	QPointer<Ui::FlatLabel> _aboutAddAdmins;

};

// Restricted box works with flags in the opposite way.
// If some flag is set in the rights then the checkbox is unchecked.

class EditRestrictedBox : public EditParticipantBox {
public:
	EditRestrictedBox(QWidget*, gsl::not_null<ChannelData*> channel, gsl::not_null<UserData*> user, bool hasAdminRights, const MTPChannelBannedRights &rights, base::lambda<void(MTPChannelBannedRights)> callback);

	static MTPChannelBannedRights DefaultRights(gsl::not_null<ChannelData*> channel);
	static constexpr auto kRestrictUntilForever = TimeId(INT_MAX);

protected:
	void prepare() override;

private:
	using Flag = MTPDchannelBannedRights::Flag;
	using Flags = MTPDchannelBannedRights::Flags;

	void applyDependencies(QPointer<Ui::Checkbox> changed);
	void showRestrictUntil();
	void setRestrictUntil(int32 until);
	bool isUntilForever() {
		return (_until <= 0) || (_until == kRestrictUntilForever);
	}

	MTPChannelBannedRights _rights;
	int32 _until = 0;
	std::vector<std::pair<Flag, Flag>> _dependencies;
	base::lambda<void(MTPChannelBannedRights)> _saveCallback;

	std::map<Flags, QPointer<Ui::Checkbox>> _checkboxes;
	QPointer<Ui::LinkButton> _restrictUntil;
	QPointer<CalendarBox> _restrictUntilBox;

};

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
class Radiobutton;
class RadiobuttonGroup;
} // namespace Ui

class CalendarBox;

class EditParticipantBox : public BoxContent {
public:
	EditParticipantBox(QWidget*, not_null<ChannelData*> channel, not_null<UserData*> user, bool hasAdminRights);

protected:
	void prepare() override;

	void resizeToContent();

	not_null<UserData*> user() const {
		return _user;
	}
	not_null<ChannelData*> channel() const {
		return _channel;
	}

	template <typename Widget>
	QPointer<Widget> addControl(object_ptr<Widget> widget, QMargins margin);

	void removeControl(QPointer<TWidget> widget);

	bool hasAdminRights() const {
		return _hasAdminRights;
	}

private:
	not_null<ChannelData*> _channel;
	not_null<UserData*> _user;
	bool _hasAdminRights = false;

	class Inner;
	QPointer<Inner> _inner;

};

class EditAdminBox : public EditParticipantBox {
public:
	EditAdminBox(QWidget*, not_null<ChannelData*> channel, not_null<UserData*> user, const MTPChannelAdminRights &rights);

	void setSaveCallback(base::lambda<void(MTPChannelAdminRights, MTPChannelAdminRights)> callback) {
		_saveCallback = std::move(callback);
	}

protected:
	void prepare() override;

private:
	using Flag = MTPDchannelAdminRights::Flag;
	using Flags = MTPDchannelAdminRights::Flags;

	static MTPChannelAdminRights DefaultRights(not_null<ChannelData*> channel);

	bool canSave() const {
		return !!_saveCallback;
	}
	void applyDependencies(QPointer<Ui::Checkbox> changed);
	void refreshAboutAddAdminsText();

	const MTPChannelAdminRights _oldRights;
	std::vector<std::pair<Flag, Flag>> _dependencies;
	base::lambda<void(MTPChannelAdminRights, MTPChannelAdminRights)> _saveCallback;

	std::map<Flags, QPointer<Ui::Checkbox>> _checkboxes;
	QPointer<Ui::FlatLabel> _aboutAddAdmins;

};

// Restricted box works with flags in the opposite way.
// If some flag is set in the rights then the checkbox is unchecked.

class EditRestrictedBox : public EditParticipantBox {
public:
	EditRestrictedBox(QWidget*, not_null<ChannelData*> channel, not_null<UserData*> user, bool hasAdminRights, const MTPChannelBannedRights &rights);

	void setSaveCallback(base::lambda<void(MTPChannelBannedRights, MTPChannelBannedRights)> callback) {
		_saveCallback = std::move(callback);
	}

protected:
	void prepare() override;

private:
	using Flag = MTPDchannelBannedRights::Flag;
	using Flags = MTPDchannelBannedRights::Flags;

	static MTPChannelBannedRights DefaultRights(not_null<ChannelData*> channel);

	bool canSave() const {
		return !!_saveCallback;
	}
	void applyDependencies(QPointer<Ui::Checkbox> changed);
	void showRestrictUntil();
	void setRestrictUntil(TimeId until);
	bool isUntilForever() {
		return ChannelData::IsRestrictedForever(_until);
	}
	void clearVariants();
	void createUntilGroup();
	void createUntilVariants();
	TimeId getRealUntilValue() const;

	const MTPChannelBannedRights _oldRights;
	TimeId _until = 0;
	std::vector<std::pair<Flag, Flag>> _dependencies;
	base::lambda<void(MTPChannelBannedRights, MTPChannelBannedRights)> _saveCallback;

	std::map<Flags, QPointer<Ui::Checkbox>> _checkboxes;

	std::shared_ptr<Ui::RadiobuttonGroup> _untilGroup;
	QVector<QPointer<Ui::Radiobutton>> _untilVariants;
	QPointer<CalendarBox> _restrictUntilBox;

	static constexpr auto kUntilOneDay = -1;
	static constexpr auto kUntilOneWeek = -2;
	static constexpr auto kUntilCustom = -3;

};

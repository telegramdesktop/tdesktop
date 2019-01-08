/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/unique_qptr.h"

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
	EditParticipantBox(
		QWidget*,
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		bool hasAdminRights);

protected:
	void prepare() override;

	not_null<UserData*> user() const {
		return _user;
	}
	not_null<ChannelData*> channel() const {
		return _channel;
	}

	template <typename Widget>
	Widget *addControl(object_ptr<Widget> widget, QMargins margin = {});

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
	EditAdminBox(
		QWidget*,
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChatAdminRights &rights);

	void setSaveCallback(
			Fn<void(MTPChatAdminRights, MTPChatAdminRights)> callback) {
		_saveCallback = std::move(callback);
	}

protected:
	void prepare() override;

private:
	using Flag = MTPDchatAdminRights::Flag;
	using Flags = MTPDchatAdminRights::Flags;

	static MTPChatAdminRights Defaults(not_null<ChannelData*> channel);

	bool canSave() const {
		return !!_saveCallback;
	}
	void refreshAboutAddAdminsText(bool canAddAdmins);

	const MTPChatAdminRights _oldRights;
	Fn<void(MTPChatAdminRights, MTPChatAdminRights)> _saveCallback;

	QPointer<Ui::FlatLabel> _aboutAddAdmins;

};

// Restricted box works with flags in the opposite way.
// If some flag is set in the rights then the checkbox is unchecked.

class EditRestrictedBox : public EditParticipantBox {
public:
	EditRestrictedBox(
		QWidget*,
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		bool hasAdminRights,
		const MTPChatBannedRights &rights);

	void setSaveCallback(
			Fn<void(MTPChatBannedRights, MTPChatBannedRights)> callback) {
		_saveCallback = std::move(callback);
	}

protected:
	void prepare() override;

private:
	using Flag = MTPDchatBannedRights::Flag;
	using Flags = MTPDchatBannedRights::Flags;

	static MTPChatBannedRights Defaults(not_null<ChannelData*> channel);

	bool canSave() const {
		return !!_saveCallback;
	}
	void showRestrictUntil();
	void setRestrictUntil(TimeId until);
	bool isUntilForever() const;
	void clearVariants();
	void createUntilGroup();
	void createUntilVariants();
	TimeId getRealUntilValue() const;

	const MTPChatBannedRights _oldRights;
	TimeId _until = 0;
	Fn<void(MTPChatBannedRights, MTPChatBannedRights)> _saveCallback;

	std::shared_ptr<Ui::RadiobuttonGroup> _untilGroup;
	std::vector<base::unique_qptr<Ui::Radiobutton>> _untilVariants;
	QPointer<CalendarBox> _restrictUntilBox;

	static constexpr auto kUntilOneDay = -1;
	static constexpr auto kUntilOneWeek = -2;
	static constexpr auto kUntilCustom = -3;

};

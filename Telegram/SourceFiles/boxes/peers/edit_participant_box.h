/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/unique_qptr.h"
#include "data/data_chat_participant_status.h"

namespace Ui {
class FlatLabel;
class LinkButton;
class Checkbox;
class Radiobutton;
class RadiobuttonGroup;
class VerticalLayout;
template <typename Widget>
class SlideWrap;
} // namespace Ui

class ChannelOwnershipTransfer;
class EditTagControl;

class EditParticipantBox : public Ui::BoxContent {
public:
	EditParticipantBox(
		QWidget*,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		bool hasAdminRights);

	[[nodiscard]] not_null<Ui::VerticalLayout*> verticalLayout() const;

protected:
	void prepare() override;

	[[nodiscard]] not_null<UserData*> user() const {
		return _user;
	}
	[[nodiscard]] not_null<PeerData*> peer() const {
		return _peer;
	}
	[[nodiscard]] bool amCreator() const;

	template <typename Widget>
	Widget *addControl(object_ptr<Widget> widget, QMargins margin = {});

	void setCoverMode(bool enabled);

	bool hasAdminRights() const {
		return _hasAdminRights;
	}

private:
	not_null<PeerData*> _peer;
	not_null<UserData*> _user;
	bool _hasAdminRights = false;

	class Inner;
	QPointer<Inner> _inner;

};

struct EditAdminBotFields {
	QString token;
	ChatAdminRights existing;
};

class EditAdminBox : public EditParticipantBox {
public:
	EditAdminBox(
		QWidget*,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		ChatAdminRightsInfo rights,
		const QString &rank,
		TimeId promotedSince,
		UserData *by,
		std::optional<EditAdminBotFields> addingBot = {});

	void setSaveCallback(
			Fn<void(
				ChatAdminRightsInfo,
				ChatAdminRightsInfo,
				const std::optional<QString> &rank)> callback) {
		_saveCallback = std::move(callback);
	}

protected:
	void prepare() override;

private:
	[[nodiscard]] ChatAdminRightsInfo defaultRights() const;

	void transferOwnership();
	[[nodiscard]] bool canSave() const {
		return _saveCallback != nullptr;
	}
	void finishAddAdmin();
	void refreshButtons();
	[[nodiscard]] bool canTransferOwnership() const;
	not_null<Ui::SlideWrap<Ui::RpWidget>*> setupTransferButton(
		not_null<Ui::VerticalLayout*> container,
		bool isGroup);

	const ChatAdminRightsInfo _oldRights;
	const QString _oldRank;
	Fn<void(
		ChatAdminRightsInfo,
		ChatAdminRightsInfo,
		const std::optional<QString> &rank)> _saveCallback;

	base::weak_qptr<Ui::BoxContent> _confirmBox;
	Ui::Checkbox *_addAsAdmin = nullptr;
	Ui::SlideWrap<Ui::VerticalLayout> *_adminControlsWrap = nullptr;
	EditTagControl *_tagControl = nullptr;

	Fn<void()> _save, _finishSave;

	TimeId _promotedSince = 0;
	UserData *_by = nullptr;
	std::optional<EditAdminBotFields> _addingBot;

	std::unique_ptr<ChannelOwnershipTransfer> _ownershipTransfer;

};

// Restricted box works with flags in the opposite way.
// If some flag is set in the rights then the checkbox is unchecked.

class EditRestrictedBox : public EditParticipantBox {
public:
	EditRestrictedBox(
		QWidget*,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		bool hasAdminRights,
		ChatRestrictionsInfo rights,
		const QString &rank,
		UserData *by,
		TimeId since);

	void setSaveCallback(
			Fn<void(ChatRestrictionsInfo, ChatRestrictionsInfo)> callback) {
		_saveCallback = std::move(callback);
	}

protected:
	void prepare() override;

private:
	[[nodiscard]] ChatRestrictionsInfo defaultRights() const;

	bool canSave() const {
		return !!_saveCallback;
	}
	void showRestrictUntil();
	void setRestrictUntil(TimeId until);
	bool isUntilForever() const;
	void createUntilGroup();
	void createUntilVariants();
	TimeId getRealUntilValue() const;

	const ChatRestrictionsInfo _oldRights;
	const QString _oldRank;
	EditTagControl *_tagControl = nullptr;
	UserData *_by = nullptr;
	TimeId _since = 0;
	TimeId _until = 0;
	Fn<void(ChatRestrictionsInfo, ChatRestrictionsInfo)> _saveCallback;

	std::shared_ptr<Ui::RadiobuttonGroup> _untilGroup;
	std::vector<base::unique_qptr<Ui::Radiobutton>> _untilVariants;

	static constexpr auto kUntilOneDay = -1;
	static constexpr auto kUntilOneWeek = -2;
	static constexpr auto kUntilCustom = -3;

};

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
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

namespace Core {
struct CloudPasswordResult;
} // namespace Core

class PasscodeBox;

class EditParticipantBox : public Ui::BoxContent {
public:
	EditParticipantBox(
		QWidget*,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		bool hasAdminRights);

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
		std::optional<EditAdminBotFields> addingBot = {});

	void setSaveCallback(
			Fn<void(
				ChatAdminRightsInfo,
				ChatAdminRightsInfo,
				const QString &rank)> callback) {
		_saveCallback = std::move(callback);
	}

protected:
	void prepare() override;

private:
	[[nodiscard]] ChatAdminRightsInfo defaultRights() const;

	not_null<Ui::InputField*> addRankInput(
		not_null<Ui::VerticalLayout*> container);
	void transferOwnership();
	void transferOwnershipChecked();
	bool handleTransferPasswordError(const QString &error);
	void requestTransferPassword(not_null<ChannelData*> channel);
	void sendTransferRequestFrom(
		QPointer<PasscodeBox> box,
		not_null<ChannelData*> channel,
		const Core::CloudPasswordResult &result);
	bool canSave() const {
		return _saveCallback != nullptr;
	}
	void finishAddAdmin();
	void refreshButtons();
	void refreshAboutAddAdminsText(bool canAddAdmins);
	bool canTransferOwnership() const;
	not_null<Ui::SlideWrap<Ui::RpWidget>*> setupTransferButton(
		not_null<Ui::VerticalLayout*> container,
		bool isGroup);

	const Ui::BoxShow _show;
	const ChatAdminRightsInfo _oldRights;
	const QString _oldRank;
	Fn<void(
		ChatAdminRightsInfo,
		ChatAdminRightsInfo,
		const QString &rank)> _saveCallback;

	QPointer<Ui::BoxContent> _confirmBox;
	Ui::Checkbox *_addAsAdmin = nullptr;
	Ui::SlideWrap<Ui::VerticalLayout> *_adminControlsWrap = nullptr;
	Ui::InputField *_rank = nullptr;
	QPointer<Ui::FlatLabel> _aboutAddAdmins;
	mtpRequestId _checkTransferRequestId = 0;
	mtpRequestId _transferRequestId = 0;
	Fn<void()> _save, _finishSave;

	std::optional<EditAdminBotFields> _addingBot;

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
		ChatRestrictionsInfo rights);

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

	const Ui::BoxShow _show;
	const ChatRestrictionsInfo _oldRights;
	TimeId _until = 0;
	Fn<void(ChatRestrictionsInfo, ChatRestrictionsInfo)> _saveCallback;

	std::shared_ptr<Ui::RadiobuttonGroup> _untilGroup;
	std::vector<base::unique_qptr<Ui::Radiobutton>> _untilVariants;

	static constexpr auto kUntilOneDay = -1;
	static constexpr auto kUntilOneWeek = -2;
	static constexpr auto kUntilCustom = -3;

};

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "data/data_peer.h"

namespace Ui {
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

class EditPeerPermissionsBox : public Ui::BoxContent {
public:
	EditPeerPermissionsBox(
		QWidget*,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer);

	struct Result {
		ChatRestrictions rights;
		int slowmodeSeconds = 0;
	};

	rpl::producer<Result> saveEvents() const;

protected:
	void prepare() override;

private:
	Fn<int()> addSlowmodeSlider(not_null<Ui::VerticalLayout*> container);
	void addSlowmodeLabels(not_null<Ui::VerticalLayout*> container);
	void addSuggestGigagroup(not_null<Ui::VerticalLayout*> container);
	void addBannedButtons(not_null<Ui::VerticalLayout*> container);

	const not_null<Window::SessionNavigation*> _navigation;
	const not_null<PeerData*> _peer;
	Ui::RoundButton *_save = nullptr;
	Fn<Result()> _value;

};

[[nodiscard]] Fn<void()> AboutGigagroupCallback(
	not_null<ChannelData*> channel);

template <typename Flags>
struct EditFlagsControl {
	object_ptr<Ui::RpWidget> widget;
	Fn<Flags()> value;
	rpl::producer<Flags> changes;
};

EditFlagsControl<ChatRestrictions> CreateEditRestrictions(
	QWidget *parent,
	rpl::producer<QString> header,
	ChatRestrictions restrictions,
	std::map<ChatRestrictions, QString> disabledMessages);

EditFlagsControl<ChatAdminRights> CreateEditAdminRights(
	QWidget *parent,
	rpl::producer<QString> header,
	ChatAdminRights rights,
	std::map<ChatAdminRights, QString> disabledMessages,
	bool isGroup,
	bool anyoneCanAddMembers);

ChatAdminRights DisabledByDefaultRestrictions(not_null<PeerData*> peer);
ChatRestrictions FixDependentRestrictions(ChatRestrictions restrictions);
ChatAdminRights AdminRightsForOwnershipTransfer(bool isGroup);

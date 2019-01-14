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

enum LangKey : int;

class EditPeerPermissionsBox : public BoxContent {
public:
	EditPeerPermissionsBox(QWidget*, not_null<PeerData*> peer);

	rpl::producer<MTPDchatBannedRights::Flags> saveEvents() const;

protected:
	void prepare() override;

private:
	void addBannedButtons(not_null<Ui::VerticalLayout*> container);

	not_null<PeerData*> _peer;
	Ui::RoundButton *_save = nullptr;
	Fn<MTPDchatBannedRights::Flags()> _value;

};

template <typename Flags>
struct EditFlagsControl {
	object_ptr<Ui::RpWidget> widget;
	Fn<Flags()> value;
	rpl::producer<Flags> changes;
};

EditFlagsControl<MTPDchatBannedRights::Flags> CreateEditRestrictions(
	QWidget *parent,
	LangKey header,
	MTPDchatBannedRights::Flags restrictions,
	MTPDchatBannedRights::Flags disabled);

EditFlagsControl<MTPDchatAdminRights::Flags> CreateEditAdminRights(
	QWidget *parent,
	LangKey header,
	MTPDchatAdminRights::Flags rights,
	MTPDchatAdminRights::Flags disabled,
	bool isGroup,
	bool anyoneCanAddMembers);

ChatAdminRights DisabledByDefaultRestrictions(not_null<PeerData*> peer);

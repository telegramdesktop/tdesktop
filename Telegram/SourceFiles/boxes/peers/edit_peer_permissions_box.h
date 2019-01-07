/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "data/data_peer.h"

enum LangKey : int;

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

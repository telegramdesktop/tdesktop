/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peers/edit_participants_box.h"
#include "boxes/peers/add_participants_box.h"

namespace Calls {
class GroupCall;
} // namespace Calls

namespace Calls::Group {

class InviteController final : public ParticipantsBoxController {
public:
	InviteController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> alreadyIn);

	void prepare() override;

	void rowClicked(not_null<PeerListRow*> row) override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

	void itemDeselectedHook(not_null<PeerData*> peer) override;

	[[nodiscard]] auto peersWithRows() const
		-> not_null<const base::flat_set<not_null<UserData*>>*>;
	[[nodiscard]] rpl::producer<not_null<UserData*>> rowAdded() const;

	[[nodiscard]] bool hasRowFor(not_null<PeerData*> peer) const;

private:
	[[nodiscard]] bool isAlreadyIn(not_null<UserData*> user) const;

	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> participant) const override;

	not_null<PeerData*> _peer;
	const base::flat_set<not_null<UserData*>> _alreadyIn;
	mutable base::flat_set<not_null<UserData*>> _inGroup;
	rpl::event_stream<not_null<UserData*>> _rowAdded;

};

class InviteContactsController final : public AddParticipantsBoxController {
public:
	InviteContactsController(
		not_null<PeerData*> peer,
		base::flat_set<not_null<UserData*>> alreadyIn,
		not_null<const base::flat_set<not_null<UserData*>>*> inGroup,
		rpl::producer<not_null<UserData*>> discoveredInGroup);

private:
	void prepareViewHook() override;

	std::unique_ptr<PeerListRow> createRow(
		not_null<UserData*> user) override;

	bool needsInviteLinkButton() override {
		return false;
	}

	const not_null<const base::flat_set<not_null<UserData*>>*> _inGroup;
	rpl::producer<not_null<UserData*>> _discoveredInGroup;

	rpl::lifetime _lifetime;

};

[[nodiscard]] object_ptr<Ui::BoxContent> PrepareInviteBox(
	not_null<GroupCall*> call,
	Fn<void(TextWithEntities&&)> showToast);

} // namespace Calls::Group

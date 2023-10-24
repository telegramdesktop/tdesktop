/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/create_giveaway_box.h"

#include "boxes/peers/edit_participants_box.h" // ParticipantsBoxController
#include "data/data_peer.h"
#include "data/data_user.h"
#include "info/info_controller.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"

namespace {

class MembersListController : public ParticipantsBoxController {
public:
	using ParticipantsBoxController::ParticipantsBoxController;

	void rowClicked(not_null<PeerListRow*> row) override;
	std::unique_ptr<PeerListRow> createRow(
		not_null<PeerData*> participant) const override;
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) override;

private:

};

void MembersListController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PeerListRow> MembersListController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user || user->isInaccessible() || user->isBot() || user->isSelf()) {
		return nullptr;
	}
	return std::make_unique<PeerListRow>(participant);
}

base::unique_qptr<Ui::PopupMenu> MembersListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

} // namespace

void CreateGiveawayBox(
		not_null<Ui::GenericBox*> box,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer) {
	box->addButton(tr::lng_box_ok(), [=] {
		auto initBox = [=](not_null<PeerListBox*> peersBox) {
			peersBox->setTitle(tr::lng_giveaway_award_option());
			peersBox->addButton(tr::lng_settings_save(), [=] {
				const auto selected = peersBox->collectSelectedRows();
				peersBox->closeBox();
			});
			peersBox->addButton(tr::lng_cancel(), [=] {
				peersBox->closeBox();
			});
		};

		box->uiShow()->showBox(
			Box<PeerListBox>(
				std::make_unique<MembersListController>(
					controller,
					peer,
					ParticipantsRole::Members),
				std::move(initBox)),
			Ui::LayerOption::KeepOther);
	});
}

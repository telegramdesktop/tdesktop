/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_history_visibility_box.h"

#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

void EditPeerHistoryVisibilityBox(
		not_null<Ui::GenericBox*> box,
		not_null<PeerData*> peer,
		Fn<void(HistoryVisibility)> savedCallback,
		HistoryVisibility historyVisibilitySavedValue) {
	const auto historyVisibility = std::make_shared<
		Ui::RadioenumGroup<HistoryVisibility>
	>(historyVisibilitySavedValue);
	peer->updateFull();

	box->setTitle(tr::lng_manage_history_visibility_title());
	box->addButton(tr::lng_settings_save(), [=] {
		savedCallback(historyVisibility->value());
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

	const auto canEdit = [&] {
		if (const auto chat = peer->asChat()) {
			return chat->canEditPreHistoryHidden();
		} else if (const auto channel = peer->asChannel()) {
			return channel->canEditPreHistoryHidden();
		}
		Unexpected("User in HistoryVisibilityEdit.");
	}();
	if (!canEdit) {
		return;
	}

	box->addSkip(st::editPeerHistoryVisibilityTopSkip);
	box->addRow(object_ptr<Ui::Radioenum<HistoryVisibility>>(
		box,
		historyVisibility,
		HistoryVisibility::Visible,
		tr::lng_manage_history_visibility_shown(tr::now),
		st::defaultBoxCheckbox));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_manage_history_visibility_shown_about(),
			st::editPeerPrivacyLabel),
		st::editPeerPreHistoryLabelMargins + st::boxRowPadding);

	box->addSkip(st::editPeerHistoryVisibilityTopSkip);
	box->addRow(object_ptr<Ui::Radioenum<HistoryVisibility>>(
		box,
		historyVisibility,
		HistoryVisibility::Hidden,
		tr::lng_manage_history_visibility_hidden(tr::now),
		st::defaultBoxCheckbox));
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			(peer->isChat()
				? tr::lng_manage_history_visibility_hidden_legacy
				: tr::lng_manage_history_visibility_hidden_about)(),
			st::editPeerPrivacyLabel),
		st::editPeerPreHistoryLabelMargins + st::boxRowPadding);
}

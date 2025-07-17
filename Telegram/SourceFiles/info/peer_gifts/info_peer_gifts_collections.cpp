/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/peer_gifts/info_peer_gifts_collections.h"

#include "api/api_credits.h" //
#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_star_gift.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace Info::PeerGifts {
namespace {

constexpr auto kCollectionNameLimit = 12;

} // namespace

void NewCollectionBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Data::SavedStarGiftId addId) {
	box->setTitle(tr::lng_gift_collection_new_title());

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_gift_collection_new_text(),
			st::collectionAbout));
	const auto title = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::collectionNameField,
			tr::lng_gift_collection_new_ph()));
	title->setMaxLength(kCollectionNameLimit * 2);
	box->setFocusCallback([=] {
		title->setFocusFast();
	});

	Ui::AddLengthLimitLabel(title, kCollectionNameLimit);

	const auto show = navigation->uiShow();
	const auto session = &peer->session();

	const auto creating = std::make_shared<bool>(false);
	box->addButton(tr::lng_gift_collection_new_create(), [=] {
		if (*creating) {
			return;
		}
		const auto text = title->getLastText().trimmed();
		if (text.isEmpty()) {
			title->showError();
			return;
		}

		*creating = true;
		auto ids = QVector<MTPInputSavedStarGift>();
		if (addId) {
			ids.push_back(Api::InputSavedStarGiftId(addId));
		}
		const auto weak = base::make_weak(box);
		session->api().request(MTPpayments_CreateStarGiftCollection(
			peer->input,
			MTP_string(text),
			MTP_vector<MTPInputSavedStarGift>(ids)
		)).done([=](const MTPStarGiftCollection &result) {
			*creating = false;
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		}).fail([=](const MTP::Error &error) {
			*creating = false;
			const auto &type = error.type();
			if (type == u""_q) {
				show->show(Ui::MakeInformBox({
					.text = tr::lng_gift_collection_limit_text(),
					.confirmText = tr::lng_box_ok(),
					.title = tr::lng_gift_collection_limit_title(),
				}));
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
			} else {
				show->showToast(error.type());
			}
		}).send();
	});

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});

}

} // namespace Info::PeerGifts

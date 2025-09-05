/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/stories/info_stories_albums.h"
#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace Info::Stories {
namespace {

constexpr auto kAlbumNameLimit = 12;

void EditAlbumBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		int id,
		StoryId addId,
		QString currentName,
		Fn<void(Data::StoryAlbum)> finished) {
	box->setTitle(id
		? tr::lng_stories_album_edit()
		: tr::lng_stories_album_new_title());

	if (!id) {
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_stories_album_new_text(),
				st::collectionAbout));
	}
	const auto title = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::collectionNameField,
			tr::lng_stories_album_new_ph(),
			currentName));
	title->setMaxLength(kAlbumNameLimit * 2);
	box->setFocusCallback([=] {
		title->setFocusFast();
	});

	Ui::AddLengthLimitLabel(title, kAlbumNameLimit);

	const auto show = navigation->uiShow();
	const auto session = &peer->session();

	const auto creating = std::make_shared<bool>(false);
	const auto submit = [=] {
		if (*creating) {
			return;
		}
		const auto text = title->getLastText().trimmed();
		if (text.isEmpty() || text.size() > kAlbumNameLimit) {
			title->showError();
			return;
		}

		*creating = true;
		const auto weak = base::make_weak(box);
		const auto done = [=](Data::StoryAlbum result) {
			*creating = false;
			if (const auto onstack = finished) {
				onstack(result);
			}
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
		const auto fail = [=](QString type) {
			*creating = false;
			if (type == u"ALBUMS_TOO_MANY"_q) {
				show->show(Ui::MakeInformBox({
					.text = tr::lng_stories_album_limit_text(),
					.confirmText = tr::lng_box_ok(),
					.title = tr::lng_stories_album_limit_title(),
				}));
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
			} else {
				show->showToast(type);
			}
		};
		if (id) {
			session->data().stories().albumRename(
				peer,
				id,
				text,
				done,
				fail);
		} else {
			session->data().stories().albumCreate(
				peer,
				text,
				addId,
				done,
				fail);
		}
	};
	title->submits() | rpl::start_with_next(submit, title->lifetime());
	auto text = id
		? tr::lng_settings_save()
		: tr::lng_stories_album_new_create();
	box->addButton(std::move(text), submit);

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

} // namespace

void NewAlbumBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		StoryId addId,
		Fn<void(Data::StoryAlbum)> added) {
	EditAlbumBox(box, navigation, peer, 0, addId, QString(), added);
}

void EditAlbumNameBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		int id,
		QString current,
		Fn<void(QString)> done) {
	EditAlbumBox(box, navigation, peer, id, {}, current, [=](
			Data::StoryAlbum result) {
		done(result.title);
	});
}

} // namespace Info::Stories

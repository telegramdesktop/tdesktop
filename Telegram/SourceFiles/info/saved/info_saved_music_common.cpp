/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_music_common.h"

#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_saved_sublist.h"
#include "history/history_item.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_music_button.h"
#include "info/saved/info_saved_music_widget.h"
#include "ui/text/format_song_document_name.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"

namespace Info::Saved {

namespace {

[[nodiscard]] Profile::MusicButtonData DocumentMusicButtonData(
		not_null<DocumentData*> document) {
	return { Ui::Text::FormatSongNameFor(document) };
}

} // namespace

void SetupSavedMusic(
		not_null<Ui::VerticalLayout*> container,
		not_null<Info::Controller*> controller,
		not_null<PeerData*> peer,
		rpl::producer<std::optional<QColor>> topBarColor) {
	auto musicValue = Data::SavedMusic::Supported(peer->id)
		? Data::SavedMusicList(
			peer,
			nullptr,
			1
		) | rpl::map([=](const Data::SavedMusicSlice &data) {
			return data.size() ? data[0].get() : nullptr;
		}) | rpl::type_erased
		: rpl::single<HistoryItem*>((HistoryItem*)(nullptr));

	const auto divider = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));

	rpl::combine(
		std::move(musicValue),
		std::move(topBarColor)
	) | rpl::on_next([=](
			HistoryItem *item,
			std::optional<QColor> color) {
		while (divider->entity()->count()) {
			delete divider->entity()->widgetAt(0);
		}
		if (item) {
			if (const auto document = item->media()
					? item->media()->document()
					: nullptr) {
				const auto music = divider->entity()->add(
					object_ptr<Profile::MusicButton>(
						divider->entity(),
						DocumentMusicButtonData(document),
						[window = controller, peer] {
							window->showSection(Info::Saved::MakeMusic(peer));
						}));
				music->setOverrideBg(color);
			}
			divider->toggle(true, anim::type::normal);
		}
	}, container->lifetime());
	divider->finishAnimating();
}

} // namespace Info::Saved
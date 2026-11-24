/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_music.h"

#include "api/api_common.h"
#include "api/api_sending.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/share_message_phrase_factory.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/stories/media_stories_controller.h"
#include "ui/controls/audio_disc_button.h"
#include "ui/image/image.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/text_entity.h"
#include "ui/widgets/menu/menu_multiline_action.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/tooltip.h"

#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Media::Stories {
namespace {

[[nodiscard]] QString MusicTitle(not_null<DocumentData*> music) {
	const auto song = music->song();
	Expects(song != nullptr);
	const auto performer = song->performer.simplified();
	return (song->title.trimmed().isEmpty() && !performer.isEmpty())
		? performer
		: Ui::Text::FormatSongNameFor(music).string().simplified();
}

} // namespace

MusicDisc::MusicDisc(
	not_null<Controller*> controller,
	not_null<DocumentData*> music,
	FullStoryId story)
: _controller(controller)
, _music(music)
, _story(story)
, _title(MusicTitle(music))
, _button(base::make_unique_q<Ui::AudioDiscButton>(
	controller->wrap(),
	Ui::AudioDiscButton::FallbackRing::SpinningArc)) {
	_music->owner().savedMusic().loadIds();
	_button->setAccessibleName(_title);
	_button->setContextMenuPolicy(Qt::PreventContextMenu);
	Ui::InstallTooltip(_button.get(), [=] { return _title; });
	_button->setClickedCallback([=] { showMenu(); });
	_controller->layoutValue() | rpl::on_next([=](const Layout &layout) {
		_button->move(
			layout.content.right() + 1 + st::mediaviewTextSkip,
			layout.content.bottom() + 1 - _button->height());
	}, _button->lifetime());

	const auto media = _music->createMediaView();
	media->thumbnailWanted(Data::FileOriginStory(_story));
	rpl::single() | rpl::then(
		_music->session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return media->thumbnail() != nullptr;
	}) | rpl::take(1) | rpl::on_next([=] {
		_button->setCover(media->thumbnail()->original());
	}, _button->lifetime());
	_button->show();
}

MusicDisc::~MusicDisc() {
	_menu = nullptr;
}

void MusicDisc::setPlaying(bool playing) {
	_button->setActive(playing);
}

void MusicDisc::showMenu() {
	if (_music->forbidsFileSave()) {
		return;
	}
	Ui::Tooltip::Hide();
	const auto show = _controller->uiShow();
	const auto session = &_music->session();
	const auto music = _music;
	const auto story = _story;
	const auto origin = Data::FileOrigin(Data::FileOriginStory(story));
	const auto savedMusic = &music->owner().savedMusic();
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_controller->wrap()->window(),
		st::storiesPopupMenuWithIcons);
	auto title = base::make_unique_q<Ui::Menu::MultilineAction>(
		_menu->menu(),
		_menu->st().menu,
		st::storiesMusicMenuTitle,
		st::historyHasCustomEmojiPosition,
		TextWithEntities{ _title });
	title->setActionTriggered([text = _title, show] {
		TextUtilities::SetClipboardText(
			TextForMimeData::Simple(text));
		show->showToast(tr::lng_text_copied(tr::now));
	});
	_menu->addAction(std::move(title));
	_menu->addSeparator();
	const auto saveToMessages = [=] {
		using namespace ChatHelpers;
		const auto self = session->user();
		auto action = Api::SendAction(session->data().history(self));
		action.replyTo.storyId = story;
		const auto sent = [=] {
			const auto args = ForwardedMessagePhraseArgs{
				.toCount = 1,
				.singleMessage = true,
				.to1 = self,
				.toSelfWithPremiumIsEmpty = false,
			};
			auto phrase = rpl::variable<TextWithEntities>(
				ForwardedMessagePhrase(args));
			auto filter = ForwardedToSavedMessagesFilter(session);
			const auto icon = ForwardedMessagePhraseIcon(args);
			show->showToast({
				.text = phrase.current(),
				.filter = std::move(filter),
				.iconLottie = icon,
				.iconLottieSize = st::toastLottieIconSize,
			});
		};
		Api::SendMusicSelection(
			Api::MessageToSend(action),
			{ { music, origin } },
			sent);
	};
	_menu->addAction(
		tr::lng_story_music_add_to_saved(tr::now),
		saveToMessages,
		&st::mediaMenuIconSavedMessages);
	if (!savedMusic->has(music)) {
		const auto saveToProfile = [=] {
			savedMusic->save(music, origin);
			const auto text = tr::lng_saved_music_added(tr::now);
			show->showToast({
				.text = { text },
				.iconLottie = u"toast/save_to_music"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		};
		_menu->addAction(
			tr::lng_story_music_add_to_profile(tr::now),
			saveToProfile,
			&st::mediaMenuIconSoundAdd);
	}
	_menu->setDestroyedCallback([controller = _controller] {
		controller->setMenuShown(false);
	});
	_controller->setMenuShown(true);
	_menu->setForcedVerticalOrigin(Ui::PopupMenu::VerticalOrigin::Bottom);
	_menu->popup(_button->mapToGlobal(QPoint()));
}

} // namespace Media::Stories

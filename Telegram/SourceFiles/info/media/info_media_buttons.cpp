/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_buttons.h"

#include "base/call_delayed.h"
#include "base/platform/base_platform_info.h"
#include "base/qt/qt_key_modifiers.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "data/components/recent_shared_media_gifts.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_stories_ids.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/view/history_view_chat_section.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/saved/info_saved_music_widget.h"
#include "info/stories/info_stories_widget.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"

namespace Info::Media {
namespace {

[[nodiscard]] bool SeparateSupported(Storage::SharedMediaType type) {
	using Type = Storage::SharedMediaType;
	return (type == Type::Photo)
		|| (type == Type::Video)
		|| (type == Type::File)
		|| (type == Type::MusicFile)
		|| (type == Type::Link)
		|| (type == Type::RoundVoiceFile)
		|| (type == Type::GIF)
		|| (type == Type::Poll);
}

[[nodiscard]] Window::SeparateId SeparateId(
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Storage::SharedMediaType type) {
	if (peer->isSelf() || !SeparateSupported(type)) {
		return { nullptr };
	}
	const auto topic = topicRootId
		? peer->forumTopicFor(topicRootId)
		: nullptr;
	if (topicRootId && !topic) {
		return { nullptr };
	}
	const auto thread = topic
			? (Data::Thread*)topic
		: peer->owner().history(peer);
	return { thread, type };
}

void AddContextMenuToButton(
		not_null<Ui::AbstractButton*> button,
		Fn<void()> openInWindow) {
	if (!openInWindow) {
		return;
	}
	button->setAcceptBoth();
	struct State final {
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = button->lifetime().make_state<State>();
	button->addClickHandler([=](Qt::MouseButton mouse) {
		if (mouse != Qt::RightButton) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button.get(),
			st::popupMenuWithIcons);
		state->menu->addAction(tr::lng_context_new_window(tr::now), [=] {
			base::call_delayed(
				st::popupMenuWithIcons.showDuration,
				crl::guard(button, openInWindow));
			}, &st::menuIconNewWindow);
		state->menu->popup(QCursor::pos());
	});
}

} // namespace

tr::phrase<lngtag_count> MediaTextPhrase(Type type) {
	switch (type) {
	case Type::Photo: return tr::lng_profile_photos;
	case Type::GIF: return tr::lng_profile_gifs;
	case Type::Video: return tr::lng_profile_videos;
	case Type::File: return tr::lng_profile_files;
	case Type::MusicFile: return tr::lng_profile_songs;
	case Type::Link: return tr::lng_profile_shared_links;
	case Type::RoundVoiceFile: return tr::lng_profile_audios;
	case Type::Poll: return tr::lng_profile_polls;
	}
	Unexpected("Type in MediaTextPhrase()");
};

Fn<QString(int)> MediaText(Type type) {
	return [phrase = MediaTextPhrase(type)](int count) {
		return phrase(tr::now, lt_count, count);
	};
}

not_null<Ui::SlideWrap<Ui::SettingsButton>*> AddCountedButton(
		Ui::VerticalLayout *parent,
		rpl::producer<int> &&count,
		Fn<QString(int)> &&textFromCount,
		Ui::MultiSlideTracker &tracker) {
	using namespace ::Settings;
	auto forked = std::move(count)
		| start_spawning(parent->lifetime());
	auto text = rpl::duplicate(
		forked
	) | rpl::map([textFromCount](int count) {
		return (count > 0)
			? textFromCount(count)
			: QString();
	});
	auto button = parent->add(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
		parent,
		object_ptr<Ui::SettingsButton>(
			parent,
			std::move(text),
			st::infoSharedMediaButton))
	)->setDuration(
		st::infoSlideDuration
	)->toggleOn(
		rpl::duplicate(forked) | rpl::map(rpl::mappers::_1 > 0)
	);
	tracker.track(button);
	return button;
};

not_null<Ui::SettingsButton*> AddButton(
		Ui::VerticalLayout *parent,
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		MsgId topicRootId,
		PeerId monoforumPeerId,
		PeerData *migrated,
		Type type,
		Ui::MultiSlideTracker &tracker) {
	auto result = AddCountedButton(
		parent,
		Profile::SharedMediaCountValue(
			peer,
			topicRootId,
			monoforumPeerId,
			migrated,
			type),
		MediaText(type),
		tracker)->entity();
	const auto separateId = SeparateId(peer, topicRootId, type);
	const auto openInWindow = separateId
		? [=] { navigation->parentController()->showInNewWindow(separateId); }
		: Fn<void()>(nullptr);
	Ui::InstallTooltip(result, [=] {
		return Platform::IsMac()
			? tr::lng_new_window_tooltip_cmd(tr::now)
			: tr::lng_new_window_tooltip_ctrl(tr::now);
	});
	AddContextMenuToButton(result, openInWindow);
	result->addClickHandler([=](Qt::MouseButton mouse) {
		if (mouse == Qt::RightButton) {
			return;
		}
		if (openInWindow
			&& (base::IsCtrlPressed() || mouse == Qt::MiddleButton)) {
			return openInWindow();
		}
		const auto topic = topicRootId
			? peer->forumTopicFor(topicRootId)
			: nullptr;
		if (topicRootId && !topic) {
			return;
		}
		const auto separateId = SeparateId(peer, topicRootId, type);
		if (Core::App().separateWindowFor(separateId) && openInWindow) {
			openInWindow();
		} else {
			navigation->showSection(topicRootId
				? std::make_shared<Info::Memento>(topic, Section(type))
				: std::make_shared<Info::Memento>(peer, Section(type)));
		}
	});
	return result;
};

} // namespace Info::Media

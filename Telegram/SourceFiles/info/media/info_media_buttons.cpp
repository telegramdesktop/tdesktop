/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_buttons.h"


#include "base/call_delayed.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_separate_id.h"
#include "styles/style_menu_icons.h"

namespace Info {
namespace Media {
namespace {

Window::SeparateSharedMediaType ToSeparateType(
		Storage::SharedMediaType type) {
	using Type = Storage::SharedMediaType;
	using SeparatedType = Window::SeparateSharedMediaType;
	return (type == Type::Photo)
		? SeparatedType::Photos
		: (type == Type::Video)
		? SeparatedType::Videos
		: (type == Type::File)
		? SeparatedType::Files
		: (type == Type::MusicFile)
		? SeparatedType::Audio
		: (type == Type::Link)
		? SeparatedType::Links
		: (type == Type::RoundVoiceFile)
		? SeparatedType::Voices
		: (type == Type::GIF)
		? SeparatedType::GIF
		: SeparatedType::None;
}

} // namespace

Fn<void()> SeparateWindowFactory(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		MsgId topicRootId,
		Storage::SharedMediaType type) {
	const auto separateType = ToSeparateType(type);
	if (separateType == Window::SeparateSharedMediaType::None) {
		return nullptr;
	}
	return [=] {
		controller->showInNewWindow({
			Window::SeparateSharedMedia(separateType, peer, topicRootId),
		});
	};
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

} // namespace Media
} // namespace Info

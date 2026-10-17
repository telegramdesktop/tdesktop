/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_emoji_status.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/ui_integration.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"

namespace EmojiStatusMenu {
namespace {

constexpr auto kToastDuration = 5 * crl::time(1000);

void ShowDoneToast(
		std::shared_ptr<ChatHelpers::Show> show,
		DocumentData *document,
		EmojiStatusId was,
		TimeId wasUntil) {
	const auto session = &show->session();
	auto text = TextWithEntities();
	if (document) {
		text.append(Ui::Text::SingleCustomEmoji(
			Data::SerializeCustomEmojiId(document->id))).append(' ');
	}
	text.append((document
		? tr::lng_emoji_status_set_done
		: tr::lng_emoji_status_remove_done)(
			tr::now,
			lt_link,
			tr::link(tr::lng_emoji_status_undo(tr::now)),
			tr::marked));
	show->showToast({
		.text = std::move(text),
		.textContext = Core::TextContext({ .session = session }),
		.filter = [=](const ClickHandlerPtr &, Qt::MouseButton button) {
			if (button != Qt::LeftButton) {
				return false;
			}
			session->data().emojiStatuses().set(was, wasUntil);
			return true;
		},
		.duration = kToastDuration,
	});
}

} // namespace

void AddSetAsStatusAction(
		const Ui::Menu::MenuCallback &addAction,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<DocumentData*> document,
		const style::icon *icon) {
	const auto session = &show->session();
	const auto sticker = document->sticker();
	if (!session->premium()
		|| !sticker
		|| sticker->setType != Data::StickersType::Emoji) {
		return;
	}
	const auto self = session->user();
	const auto was = self->emojiStatusId();
	const auto wasUntil = session->data().emojiStatuses().automaticClearAt(
		self);
	const auto already = !was.collectible && (was.documentId == document->id);
	const auto raw = already ? nullptr : document.get();
	addAction((already
		? tr::lng_emoji_status_remove
		: tr::lng_emoji_status_for_submit)(tr::now), [=] {
		session->data().emojiStatuses().set(already
			? EmojiStatusId()
			: EmojiStatusId{ .documentId = document->id });
		ShowDoneToast(show, raw, was, wasUntil);
	}, icon);
}

} // namespace EmojiStatusMenu

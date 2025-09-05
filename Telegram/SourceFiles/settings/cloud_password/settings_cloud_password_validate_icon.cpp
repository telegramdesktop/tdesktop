/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_validate_icon.h"

#include "apiwrap.h"
#include "base/object_ptr.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "main/main_session.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

[[nodiscard]] DocumentData *EmojiValidateGood(
		not_null<Main::Session*> session) {
	auto emoji = TextWithEntities{
		.text = (QString(QChar(0xD83D)) + QChar(0xDC4D)),
	};
	if (const auto e = Ui::Emoji::Find(emoji.text)) {
		const auto sticker = session->emojiStickersPack().stickerForEmoji(e);
		return sticker.document;
	}
	return nullptr;
}

} // namespace

object_ptr<Ui::RpWidget> CreateValidateGoodIcon(
		not_null<Main::Session*> session) {
	const auto document = EmojiValidateGood(session);
	if (!document) {
		return nullptr;
	}

	auto owned = object_ptr<Ui::RpWidget>((QWidget*)nullptr);
	const auto widget = owned.data();

	struct State {
		std::unique_ptr<Ui::Text::CustomEmoji> emoji;
	};
	const auto state = widget->lifetime().make_state<State>();
	const auto size = st::settingsCloudPasswordIconSize;
	state->emoji = std::make_unique<Ui::Text::LimitedLoopsEmoji>(
		session->data().customEmojiManager().create(
			document,
			[=] { widget->update(); },
			Data::CustomEmojiManager::SizeTag::Normal,
			size),
		1,
		true);
	widget->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(widget);
		state->emoji->paint(p, Ui::Text::CustomEmojiPaintContext{
			.textColor = st::windowFg->c,
			.now = crl::now(),
		});
	}, widget->lifetime());
	const auto padding = st::settingLocalPasscodeIconPadding;
	widget->resize((Rect(Size(size)) + padding).size());
	widget->setNaturalWidth(padding.left() + size + padding.right());

	return owned;
}

} // namespace Settings

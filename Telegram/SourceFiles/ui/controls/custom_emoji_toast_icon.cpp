/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/custom_emoji_toast_icon.h"

#include "data/stickers/data_custom_emoji.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/rp_widget.h"

#include "styles/style_widgets.h"

#include <QtGui/QPainter>

namespace Ui {

object_ptr<RpWidget> MakeCustomEmojiToastIcon(
		not_null<Main::Session*> session,
		DocumentId emojiId,
		QSize size) {
	auto result = object_ptr<RpWidget>((QWidget*)nullptr);
	const auto raw = result.data();
	raw->resize(size);
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);

	struct State {
		std::unique_ptr<Text::CustomEmoji> emoji;
	};
	const auto state = raw->lifetime().make_state<State>();
	state->emoji = session->data().customEmojiManager().create(
		emojiId,
		[=] { raw->update(); },
		Data::CustomEmojiManager::SizeTag::Large);

	raw->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(raw);
		const auto esize = Emoji::GetCustomSizeLarge();
		const auto position = QPoint(
			(raw->width() - esize) / 2,
			(raw->height() - esize) / 2);
		state->emoji->paint(p, {
			.textColor = st::toastFg->c,
			.now = crl::now(),
			.position = position,
		});
	}, raw->lifetime());

	return result;
}

} // namespace Ui

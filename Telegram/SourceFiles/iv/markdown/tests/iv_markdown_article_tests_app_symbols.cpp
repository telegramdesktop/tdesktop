/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot.h"
#include "base/weak_ptr.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"

// WHY: layout links these app entries; tests use generated English and never click or paint.
namespace Lang::details {

QString Current(ushort key) {
	return GetOriginalValue(key);
}

} // namespace Lang::details

namespace Api {

void ActivateRichPageBotButton(
		ClickHandlerContext context,
		const HistoryMessageMarkupButton &button) {
	Unexpected("Api::ActivateRichPageBotButton in td_iv_article_test.");
}

} // namespace Api

namespace Data {

HistoryItem *Session::message(FullMsgId itemId) const {
	Unexpected("Data::Session::message in td_iv_article_test.");
}

} // namespace Data

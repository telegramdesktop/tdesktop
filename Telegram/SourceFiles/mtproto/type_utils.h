/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "scheme.h"
#include "base/flags.h"

inline MTPbool MTP_bool(bool v) {
	return v ? MTP_boolTrue() : MTP_boolFalse();
}

inline bool mtpIsTrue(const MTPBool &v) {
	return v.type() == mtpc_boolTrue;
}
inline bool mtpIsFalse(const MTPBool &v) {
	return !mtpIsTrue(v);
}

// we must validate that MTProto scheme flags don't intersect with client side flags
// and define common bit operators which allow use Type_ClientFlag together with Type::Flag
#define DEFINE_MTP_CLIENT_FLAGS(Type) \
static_assert(Type::Flags(Type::Flag::MAX_FIELD) < static_cast<Type::Flag>(Type##_ClientFlag::MIN_FIELD), \
	"MTProto flags conflict with client side flags!"); \
namespace base {\
	template<>\
	struct extended_flags<Type##_ClientFlag> {\
		using type = Type::Flag;\
	};\
}

// we use the same flags field for some additional client side flags
enum class MTPDmessage_ClientFlag : uint32 {
	// message has links for "shared links" indexing
	f_has_text_links = (1U << 30),

	// message is a group migrate (group -> supergroup) service message
	f_is_group_migrate = (1U << 29),

	// message needs initDimensions() + resize() + paint()
	f_pending_init_dimensions = (1U << 28),

	// message needs resize() + paint()
	f_pending_resize = (1U << 27),

	// message needs paint()
	f_pending_paint = (1U << 26),

	// message is attached to previous one when displaying the history
	f_attach_to_previous = (1U << 25),

	// message is attached to next one when displaying the history
	f_attach_to_next = (1U << 24),

	// message was sent from inline bot, need to re-set media when sent
	f_from_inline_bot = (1U << 23),

	// message has a switch inline keyboard button, need to return to inline
	f_has_switch_inline_button = (1U << 22),

	// message is generated on the client side and should be unread
	f_clientside_unread = (1U << 21),

	// message has an admin badge in supergroup
	f_has_admin_badge = (1U << 20),

	// message is not displayed because it is part of a group
	f_hidden_by_group = (1U << 19),

	// update this when adding new client side flags
	MIN_FIELD = (1U << 19),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDmessage)

enum class MTPDreplyKeyboardMarkup_ClientFlag : uint32 {
	// none (zero) markup
	f_zero = (1U << 30),

	// markup just wants a text reply
	f_force_reply = (1U << 29),

	// markup keyboard is inline
	f_inline = (1U << 28),

	// markup has a switch inline keyboard button
	f_has_switch_inline_button = (1U << 27),

	// update this when adding new client side flags
	MIN_FIELD = (1U << 27),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDreplyKeyboardMarkup)

enum class MTPDstickerSet_ClientFlag : uint32 {
	// old value for sticker set is not yet loaded flag
	f_not_loaded__old = (1U << 31),

	// sticker set is not yet loaded
	f_not_loaded = (1U << 30),

	// sticker set is one of featured (should be saved locally)
	f_featured = (1U << 29),

	// sticker set is an unread featured set
	f_unread = (1U << 28),

	// special set like recent or custom stickers
	f_special = (1U << 27),

	// update this when adding new client side flags
	MIN_FIELD = (1U << 27),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDstickerSet)

//enum class MTPDuser_ClientFlag : uint32 {
//	// forbidden constructor received
//	f_inaccessible = (1U << 31),
//
//	// update this when adding new client side flags
//	MIN_FIELD = (1U << 31),
//};
//DEFINE_MTP_CLIENT_FLAGS(MTPDuser)

enum class MTPDchat_ClientFlag : uint32 {
	// forbidden constructor received
	f_forbidden = (1U << 31),

	// update this when adding new client side flags
	MIN_FIELD = (1U << 31),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDchat)

enum class MTPDchannel_ClientFlag : uint32 {
	// forbidden constructor received
	f_forbidden = (1U << 31),

	// update this when adding new client side flags
	MIN_FIELD = (1U << 31),
};
DEFINE_MTP_CLIENT_FLAGS(MTPDchannel)

extern const MTPReplyMarkup MTPnullMarkup;
extern const MTPVector<MTPMessageEntity> MTPnullEntities;
extern const MTPMessageFwdHeader MTPnullFwdHeader;

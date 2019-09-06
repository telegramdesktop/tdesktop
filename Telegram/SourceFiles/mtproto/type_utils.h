/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	// message is a group / channel create or migrate service message
	f_is_group_essential = (1U << 29),

	// message's edited media is generated on the client
	// and should not update media from server
	f_is_local_update_media = (1U << 28),

	// message was sent from inline bot, need to re-set media when sent
	f_from_inline_bot = (1U << 27),

	// message has a switch inline keyboard button, need to return to inline
	f_has_switch_inline_button = (1U << 26),

	// message is generated on the client side and should be unread
	f_clientside_unread = (1U << 25),

	// message has an admin badge in supergroup
	f_has_admin_badge = (1U << 24),

	// message is an outgoing message that is being sent
	f_sending = (1U << 23),

	// message was an outgoing message and failed to be sent
	f_failed = (1U << 22),

	// message has no media and only a several emoji text
	f_isolated_emoji = (1U << 21),

	// message is local message existing in the message history
	f_local_history_entry = (1U << 20),

	// message is an admin log entry
	f_admin_log_entry = (1U << 19),

	// message is a fake message for some ui
	f_fake_history_item = (1U << 18),
};
inline constexpr bool is_flag_type(MTPDmessage_ClientFlag) { return true; }
using MTPDmessage_ClientFlags = base::flags<MTPDmessage_ClientFlag>;

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

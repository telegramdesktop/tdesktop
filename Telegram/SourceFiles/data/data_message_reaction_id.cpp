/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include "data/data_message_reaction_id.h"

#include "data/stickers/data_custom_emoji.h"

namespace Data {

QString ReactionEntityData(const ReactionId &id) {
	if (id.empty()) {
		return {};
	} else if (const auto custom = id.custom()) {
		return SerializeCustomEmojiId(custom);
	}
	return u"default:"_q + id.emoji();
}

ReactionId ReactionFromMTP(const MTPReaction &reaction) {
	return reaction.match([](MTPDreactionEmpty) {
		return ReactionId{ QString() };
	}, [](const MTPDreactionEmoji &data) {
		return ReactionId{ qs(data.vemoticon()) };
	}, [](const MTPDreactionCustomEmoji &data) {
		return ReactionId{ DocumentId(data.vdocument_id().v) };
	});
}

MTPReaction ReactionToMTP(ReactionId id) {
	if (const auto custom = id.custom()) {
		return MTP_reactionCustomEmoji(MTP_long(custom));
	}
	const auto emoji = id.emoji();
	return emoji.isEmpty()
		? MTP_reactionEmpty()
		: MTP_reactionEmoji(MTP_string(emoji));
}

} // namespace Data

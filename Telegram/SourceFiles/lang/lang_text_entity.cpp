/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_text_entity.h"

#include "lang/lang_tag.h"

namespace Lang {

TextWithEntities ReplaceTag<TextWithEntities>::Call(TextWithEntities &&original, ushort tag, const TextWithEntities &replacement) {
	auto replacementPosition = FindTagReplacementPosition(original.text, tag);
	if (replacementPosition < 0) {
		return std::move(original);
	}

	auto result = TextWithEntities();
	result.text = ReplaceTag<QString>::Replace(std::move(original.text), replacement.text, replacementPosition);
	auto originalEntitiesCount = original.entities.size();
	auto replacementEntitiesCount = replacement.entities.size();
	if (originalEntitiesCount != 0 || replacementEntitiesCount != 0) {
		result.entities.reserve(originalEntitiesCount + replacementEntitiesCount);

		auto replacementEnd = replacementPosition + int(replacement.text.size());
		auto replacementEntity = replacement.entities.cbegin();
		auto addReplacementEntitiesUntil = [&replacementEntity, &replacement, &result, replacementPosition, replacementEnd](int untilPosition) {
			while (replacementEntity != replacement.entities.cend()) {
				auto newOffset = replacementPosition + replacementEntity->offset();
				if (newOffset >= untilPosition) {
					return;
				}
				auto newEnd = newOffset + replacementEntity->length();
				newOffset = std::clamp(newOffset, replacementPosition, replacementEnd);
				newEnd = std::clamp(newEnd, replacementPosition, replacementEnd);
				if (auto newLength = newEnd - newOffset) {
					result.entities.push_back({ replacementEntity->type(), newOffset, newLength, replacementEntity->data() });
				}
				++replacementEntity;
			}
		};

		for (const auto &entity : std::as_const(original.entities)) {
			// Transform the entity by the replacement.
			auto offset = entity.offset();
			auto end = offset + entity.length();
			if (offset > replacementPosition) {
				offset = offset + replacement.text.size() - kTagReplacementSize;
			}
			if (end > replacementPosition) {
				end = end + replacement.text.size() - kTagReplacementSize;
			}
			offset = std::clamp(offset, 0, int(result.text.size()));
			end = std::clamp(end, 0, int(result.text.size()));

			// Add all replacement entities that start before the current original entity.
			addReplacementEntitiesUntil(offset);

			// Add a modified original entity.
			if (auto length = end - offset) {
				result.entities.push_back({ entity.type(), offset, length, entity.data() });
			}
		}
		// Add the remaining replacement entities.
		addReplacementEntitiesUntil(result.text.size());
	}
	return result;
}

} // namespace Lang

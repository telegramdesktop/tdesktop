/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_text_entities.h"

#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "ui/widgets/fields/input_field.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace Iv::Editor {
namespace {

struct FormulaReplacement {
	int offset = 0;
	int length = 0;
	QString source;
};

struct TextRange {
	int offset = 0;
	int length = 0;
};

[[nodiscard]] bool RangeInsideText(
		const QString &text,
		int offset,
		int length) {
	return (offset >= 0)
		&& (length >= 0)
		&& (offset <= text.size())
		&& (length <= text.size() - offset);
}

[[nodiscard]] bool IsFormulaObjectSpan(
		const QString &text,
		const EntityInText &entity) {
	return (entity.length() == 1)
		&& RangeInsideText(text, entity.offset(), entity.length())
		&& (text[entity.offset()] == QChar::ObjectReplacementCharacter);
}

[[nodiscard]] std::optional<Markdown::InlineTextObjectFormulaData>
FormulaDataFromEntity(const EntityInText &entity) {
	if (entity.type() != EntityType::CustomEmoji) {
		return std::nullopt;
	}
	const auto parsed = Markdown::ParseInlineTextObjectEntity(entity.data());
	if (!parsed || parsed->kind != Markdown::InlineTextObjectKind::Formula) {
		return std::nullopt;
	}
	const auto formula = std::get_if<Markdown::InlineTextObjectFormulaData>(
		&parsed->data);
	return formula ? std::make_optional(*formula) : std::nullopt;
}

[[nodiscard]] QString EditorSourceForFormula(
		const Markdown::InlineTextObjectFormulaData &formula) {
	const auto stripDelimiters = [](QString result) {
		result = result.trimmed();
		if (result.size() >= 2
			&& result.front() == QChar('$')
			&& result.back() == QChar('$')) {
			result = result.mid(1, result.size() - 2).trimmed();
		}
		return result;
	};
	if (!formula.trimmedTex.isEmpty()) {
		return Markdown::InlineFormulaCopySource(
			stripDelimiters(formula.trimmedTex));
	}
	return Markdown::InlineFormulaCopySource(
		stripDelimiters(formula.copySource));
}

[[nodiscard]] std::optional<EntityInText> AdjustEntityForReplacement(
		const EntityInText &entity,
		int from,
		int oldLength,
		int newLength) {
	const auto till = from + oldLength;
	const auto delta = newLength - oldLength;
	const auto begin = entity.offset();
	const auto end = begin + entity.length();

	if (end <= from) {
		return entity;
	} else if (begin >= till) {
		return EntityInText(
			entity.type(),
			begin + delta,
			entity.length(),
			entity.data());
	} else if (begin <= from && end >= till) {
		const auto length = entity.length() + delta;
		if (length <= 0) {
			return std::nullopt;
		}
		return EntityInText(entity.type(), begin, length, entity.data());
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<TextWithTags::Tag> AdjustTagForReplacement(
		const TextWithTags::Tag &tag,
		int from,
		int oldLength,
		int newLength) {
	const auto till = from + oldLength;
	const auto delta = newLength - oldLength;
	const auto begin = tag.offset;
	const auto end = begin + tag.length;

	if (end <= from) {
		return tag;
	} else if (begin >= till) {
		return TextWithTags::Tag{
			.offset = begin + delta,
			.length = tag.length,
			.id = tag.id,
		};
	} else if (begin <= from && end >= till) {
		const auto length = tag.length + delta;
		if (length <= 0) {
			return std::nullopt;
		}
		return TextWithTags::Tag{
			.offset = begin,
			.length = length,
			.id = tag.id,
		};
	}
	return std::nullopt;
}

[[nodiscard]] QString TagsWithoutIvEditorTags(QStringView tags) {
	auto result = QList<QStringView>();
	for (const auto &tag : TextUtilities::SplitTags(tags)) {
		if (!Ui::InputField::IsInstantViewEditorTag(tag)
			&& !tag.startsWith(QChar('#'))) {
			result.push_back(tag);
		}
	}
	return TextUtilities::JoinTag(result);
}

[[nodiscard]] TextWithTags::Tags GenericTagsWithoutIvEditorTags(
		const TextWithTags::Tags &tags) {
	auto result = TextWithTags::Tags();
	result.reserve(tags.size());
	for (const auto &tag : tags) {
		auto filtered = TagsWithoutIvEditorTags(tag.id);
		if (!filtered.isEmpty()) {
			result.push_back({
				.offset = tag.offset,
				.length = tag.length,
				.id = filtered,
			});
		}
	}
	return result;
}

[[nodiscard]] bool IsValidAnchorEntity(const EntityInText &entity) {
	if (entity.type() != EntityType::CustomUrl
		|| !Ui::InputField::IsInstantViewAnchorLink(entity.data())) {
		return false;
	}
	return !Markdown::NormalizeFragmentId(entity.data().mid(1)).isEmpty();
}

void SortTags(TextWithTags::Tags *tags);

void AppendIvEntityTags(
		TextWithTags::Tags *tags,
		const EntitiesInText &entities) {
	for (const auto &entity : entities) {
		if (entity.length() <= 0) {
			continue;
		}
		switch (entity.type()) {
		case EntityType::Subscript:
			tags->push_back({
				.offset = entity.offset(),
				.length = entity.length(),
				.id = Ui::InputField::kTagIvSubscript,
			});
			break;
		case EntityType::Superscript:
			tags->push_back({
				.offset = entity.offset(),
				.length = entity.length(),
				.id = Ui::InputField::kTagIvSuperscript,
			});
			break;
		case EntityType::Marked:
			tags->push_back({
				.offset = entity.offset(),
				.length = entity.length(),
				.id = Ui::InputField::kTagIvMarked,
			});
			break;
		case EntityType::CustomUrl:
			if (IsValidAnchorEntity(entity)) {
				tags->push_back({
					.offset = entity.offset(),
					.length = entity.length(),
					.id = entity.data(),
				});
			}
			break;
		default:
			break;
		}
	}
}

void OverlayTag(
		TextWithTags::Tags *tags,
		const TextWithTags::Tag &overlay,
		const QString &text) {
	if (overlay.id.isEmpty()
		|| overlay.length <= 0
		|| !RangeInsideText(text, overlay.offset, overlay.length)) {
		return;
	}
	const auto from = overlay.offset;
	const auto till = from + overlay.length;
	auto coveredTill = from;
	auto result = TextWithTags::Tags();
	result.reserve(tags->size() + 3);

	for (const auto &tag : *tags) {
		const auto tagFrom = tag.offset;
		const auto tagTill = tag.offset + tag.length;
		if (tagTill <= from) {
			result.push_back(tag);
			continue;
		} else if (tagFrom >= till) {
			if (coveredTill < till) {
				result.push_back({
					.offset = coveredTill,
					.length = till - coveredTill,
					.id = overlay.id,
				});
				coveredTill = till;
			}
			result.push_back(tag);
			continue;
		}
		if (tagFrom > coveredTill) {
			result.push_back({
				.offset = coveredTill,
				.length = tagFrom - coveredTill,
				.id = overlay.id,
			});
			coveredTill = tagFrom;
		}
		if (tagFrom < from) {
			result.push_back({
				.offset = tagFrom,
				.length = from - tagFrom,
				.id = tag.id,
			});
		}
		const auto middleFrom = std::max(tagFrom, from);
		const auto middleTill = std::min(tagTill, till);
		if (middleFrom < middleTill) {
			result.push_back({
				.offset = middleFrom,
				.length = middleTill - middleFrom,
				.id = TextUtilities::TagWithAdded(tag.id, overlay.id),
			});
			coveredTill = middleTill;
		}
		if (tagTill > till) {
			result.push_back({
				.offset = till,
				.length = tagTill - till,
				.id = tag.id,
			});
		}
	}
	if (coveredTill < till) {
		result.push_back({
			.offset = coveredTill,
			.length = till - coveredTill,
			.id = overlay.id,
		});
	}
	SortTags(&result);
	*tags = TextUtilities::SimplifyTags(std::move(result));
}

void OverlayTags(
		TextWithTags::Tags *tags,
		const TextWithTags::Tags &overlays,
		const QString &text) {
	for (const auto &overlay : overlays) {
		OverlayTag(tags, overlay, text);
	}
}

void SubtractRange(
		std::vector<TextRange> *ranges,
		int from,
		int till) {
	auto result = std::vector<TextRange>();
	for (const auto &range : *ranges) {
		const auto rangeFrom = range.offset;
		const auto rangeTill = range.offset + range.length;
		if (rangeTill <= from || rangeFrom >= till) {
			result.push_back(range);
			continue;
		}
		if (rangeFrom < from) {
			result.push_back({
				.offset = rangeFrom,
				.length = from - rangeFrom,
			});
		}
		if (rangeTill > till) {
			result.push_back({
				.offset = till,
				.length = rangeTill - till,
			});
		}
	}
	*ranges = std::move(result);
}

void RemoveRangesFromTags(
		TextWithTags::Tags *tags,
		const TextWithTags::Tags &removed) {
	auto result = TextWithTags::Tags();
	for (const auto &tag : *tags) {
		auto ranges = std::vector<TextRange>{ {
			.offset = tag.offset,
			.length = tag.length,
		} };
		for (const auto &remove : removed) {
			SubtractRange(
				&ranges,
				remove.offset,
				remove.offset + remove.length);
		}
		for (const auto &range : ranges) {
			if (range.length > 0) {
				result.push_back({
					.offset = range.offset,
					.length = range.length,
					.id = tag.id,
				});
			}
		}
	}
	SortTags(&result);
	*tags = TextUtilities::SimplifyTags(std::move(result));
}

void SortTags(TextWithTags::Tags *tags) {
	std::sort(tags->begin(), tags->end(), [](const auto &a, const auto &b) {
		if (a.offset != b.offset) {
			return a.offset < b.offset;
		} else if (a.length != b.length) {
			return a.length < b.length;
		}
		return a.id < b.id;
	});
}

[[nodiscard]] bool TagContains(QStringView tags, QStringView tagId) {
	return TextUtilities::SplitTags(tags).contains(tagId);
}

[[nodiscard]] bool TagContainsOtherThan(QStringView tags, QStringView tagId) {
	for (const auto &tag : TextUtilities::SplitTags(tags)) {
		if (tag != tagId) {
			return true;
		}
	}
	return false;
}

void MergeRanges(std::vector<TextRange> *ranges);

[[nodiscard]] std::vector<TextRange> RangesContainingTagId(
		const TextWithTags::Tags &tags,
		QStringView tagId,
		const QString &text) {
	auto result = std::vector<TextRange>();
	result.reserve(tags.size());
	for (const auto &tag : tags) {
		if (tag.length <= 0
			|| !RangeInsideText(text, tag.offset, tag.length)
			|| !TagContains(tag.id, tagId)) {
			continue;
		}
		result.push_back({
			.offset = tag.offset,
			.length = tag.length,
		});
	}
	return result;
}

[[nodiscard]] EntitiesInText ValidAnchorEntitiesFromTags(
		const TextWithTags::Tags &tags,
		const QString &text) {
	auto result = EntitiesInText();
	result.reserve(tags.size());
	for (const auto &tag : tags) {
		if (tag.length <= 0 || !RangeInsideText(text, tag.offset, tag.length)) {
			continue;
		}
		for (const auto &single : TextUtilities::SplitTags(tag.id)) {
			if (!Ui::InputField::IsInstantViewAnchorLink(single)) {
				continue;
			}
			auto link = single.toString();
			if (Markdown::NormalizeFragmentId(link).isEmpty()) {
				continue;
			}
			result.push_back(EntityInText(
				EntityType::CustomUrl,
				tag.offset,
				tag.length,
				link));
		}
	}
	return result;
}

[[nodiscard]] std::vector<TextRange> MathRangesWithoutIntersections(
		const TextWithTags::Tags &tags,
		const QString &text) {
	auto result = std::vector<TextRange>();
	for (const auto &math : tags) {
		if (math.length <= 0
			|| !RangeInsideText(text, math.offset, math.length)
			|| !TagContains(math.id, Ui::InputField::kTagIvMath)) {
			continue;
		}
		auto ranges = std::vector<TextRange>{ {
			.offset = math.offset,
			.length = math.length,
		} };
		for (const auto &other : tags) {
			if (other.length <= 0
				|| !RangeInsideText(text, other.offset, other.length)
				|| !TagContainsOtherThan(
					other.id,
					Ui::InputField::kTagIvMath)) {
				continue;
			}
			SubtractRange(
				&ranges,
				other.offset,
				other.offset + other.length);
		}
		result.insert(result.end(), ranges.begin(), ranges.end());
	}
	MergeRanges(&result);
	return result;
}

void AppendEntitiesForTagId(
		EntitiesInText *entities,
		const TextWithTags::Tags &tags,
		QStringView tagId,
		EntityType type,
		const QString &text) {
	for (const auto &range : RangesContainingTagId(tags, tagId, text)) {
		entities->push_back(EntityInText(type, range.offset, range.length));
	}
}

void MergeRanges(std::vector<TextRange> *ranges) {
	std::sort(ranges->begin(), ranges->end(), [](const auto &a, const auto &b) {
		if (a.offset != b.offset) {
			return a.offset < b.offset;
		}
		return a.length < b.length;
	});

	auto result = std::vector<TextRange>();
	result.reserve(ranges->size());
	for (const auto &range : *ranges) {
		if (result.empty()) {
			result.push_back(range);
			continue;
		}
		auto &last = result.back();
		const auto lastEnd = last.offset + last.length;
		const auto rangeEnd = range.offset + range.length;
		if (range.offset > lastEnd) {
			result.push_back(range);
		} else if (rangeEnd > lastEnd) {
			last.length = rangeEnd - last.offset;
		}
	}
	*ranges = std::move(result);
}

void SortEntities(EntitiesInText *entities) {
	std::sort(
		entities->begin(),
		entities->end(),
		[](const auto &a, const auto &b) {
			if (a.offset() != b.offset()) {
				return a.offset() < b.offset();
			} else if (a.length() != b.length()) {
				return a.length() < b.length();
			} else if (a.type() != b.type()) {
				return int(a.type()) < int(b.type());
			}
			return a.data() < b.data();
		});
}

} // namespace

RichTextEditorConversion ConvertRichTextToEditorTags(TextWithEntities text) {
	auto formulas = std::vector<FormulaReplacement>();
	auto entities = EntitiesInText();
	entities.reserve(text.entities.size());

	for (const auto &entity : text.entities) {
		const auto formula = FormulaDataFromEntity(entity);
		if (formula && IsFormulaObjectSpan(text.text, entity)) {
			formulas.push_back({
				.offset = entity.offset(),
				.length = entity.length(),
				.source = EditorSourceForFormula(*formula),
			});
		} else {
			entities.push_back(entity);
		}
	}

	std::sort(
		formulas.begin(),
		formulas.end(),
		[](const auto &a, const auto &b) {
			return a.offset > b.offset;
		});

	auto mathTags = TextWithTags::Tags();
	auto replacements = std::vector<RichTextEditorOffsetReplacement>();
	replacements.reserve(formulas.size());

	for (const auto &formula : formulas) {
		const auto newLength = formula.source.size();

		for (auto i = entities.begin(); i != entities.end();) {
			if (const auto adjusted = AdjustEntityForReplacement(
					*i,
					formula.offset,
					formula.length,
					newLength)) {
				*i++ = *adjusted;
			} else {
				i = entities.erase(i);
			}
		}
		for (auto i = mathTags.begin(); i != mathTags.end();) {
			if (const auto adjusted = AdjustTagForReplacement(
					*i,
					formula.offset,
					formula.length,
					newLength)) {
				*i++ = *adjusted;
			} else {
				i = mathTags.erase(i);
			}
		}

		text.text.replace(formula.offset, formula.length, formula.source);
		if (newLength > 0) {
			mathTags.push_back({
				.offset = formula.offset,
				.length = int(newLength),
				.id = Ui::InputField::kTagIvMath,
			});
		}
		replacements.push_back({
			.richOffset = formula.offset,
			.richLength = formula.length,
			.editorLength = int(newLength),
		});
	}

	auto tags = TextUtilities::ConvertEntitiesToTextTags(entities);
	auto ivTags = TextWithTags::Tags();
	AppendIvEntityTags(&ivTags, entities);
	RemoveRangesFromTags(&tags, mathTags);
	RemoveRangesFromTags(&ivTags, mathTags);
	OverlayTags(&tags, ivTags, text.text);
	OverlayTags(&tags, mathTags, text.text);
	SortTags(&tags);
	tags = TextUtilities::SimplifyTags(tags);

	std::sort(
		replacements.begin(),
		replacements.end(),
		[](const auto &a, const auto &b) {
			return a.richOffset < b.richOffset;
		});

	return {
		.text = { text.text, tags },
		.replacements = replacements,
	};
}

TextWithEntities FormulaSourceToRichText(QString source) {
	const auto length = int(source.size());
	return ConvertEditorTagsToRichText(TextWithTags{
		.text = std::move(source),
		.tags = (length > 0)
			? TextWithTags::Tags{ TextWithTags::Tag{
				.offset = 0,
				.length = length,
				.id = Ui::InputField::kTagIvMath,
			} }
			: TextWithTags::Tags(),
	});
}

int MapRichTextOffsetToEditorOffset(
		const std::vector<RichTextEditorOffsetReplacement> &replacements,
		int offset) {
	auto delta = 0;
	for (const auto &replacement : replacements) {
		if (replacement.richLength <= 0) {
			continue;
		}
		const auto richStart = replacement.richOffset;
		const auto richEnd = richStart + replacement.richLength;
		const auto editorStart = richStart + delta;
		if (offset < richStart) {
			break;
		} else if (offset <= richEnd) {
			return editorStart
				+ ((offset == richEnd) ? replacement.editorLength : 0);
		}
		delta += replacement.editorLength - replacement.richLength;
	}
	return offset + delta;
}

TextWithEntities ConvertEditorTagsToRichText(TextWithTags text) {
	auto entities = TextUtilities::ConvertTextTagsToEntities(
		GenericTagsWithoutIvEditorTags(text.tags));
	AppendEntitiesForTagId(
		&entities,
		text.tags,
		Ui::InputField::kTagIvMarked,
		EntityType::Marked,
		text.text);
	AppendEntitiesForTagId(
		&entities,
		text.tags,
		Ui::InputField::kTagIvSubscript,
		EntityType::Subscript,
		text.text);
	AppendEntitiesForTagId(
		&entities,
		text.tags,
		Ui::InputField::kTagIvSuperscript,
		EntityType::Superscript,
		text.text);
	for (const auto &entity : ValidAnchorEntitiesFromTags(
			text.tags,
			text.text)) {
		entities.push_back(entity);
	}

	auto mathRanges = MathRangesWithoutIntersections(text.tags, text.text);
	for (auto i = mathRanges.rbegin(); i != mathRanges.rend(); ++i) {
		const auto source = text.text.mid(i->offset, i->length);
		auto trimmedSource = source.trimmed();
		if (trimmedSource.size() >= 2
			&& trimmedSource.front() == QChar('$')
			&& trimmedSource.back() == QChar('$')) {
			trimmedSource = trimmedSource
				.mid(1, trimmedSource.size() - 2)
				.trimmed();
		}
		if (trimmedSource.isEmpty()) {
			continue;
		}
		const auto entityData = Markdown::SerializeInlineTextObjectEntity({
			.kind = Markdown::InlineTextObjectKind::Formula,
			.data = Markdown::InlineTextObjectFormulaData{
				.copySource = Markdown::InlineFormulaCopySource(trimmedSource),
				.trimmedTex = trimmedSource,
			},
		});
		for (auto j = entities.begin(); j != entities.end();) {
			if (const auto adjusted = AdjustEntityForReplacement(
					*j,
					i->offset,
					i->length,
					1)) {
				*j++ = *adjusted;
			} else {
				j = entities.erase(j);
			}
		}
		text.text.replace(
			i->offset,
			i->length,
			QString(QChar::ObjectReplacementCharacter));
		entities.push_back(EntityInText(
			EntityType::CustomEmoji,
			i->offset,
			1,
			entityData));
	}

	SortEntities(&entities);
	return {
		.text = text.text,
		.entities = entities,
	};
}

} // namespace Iv::Editor

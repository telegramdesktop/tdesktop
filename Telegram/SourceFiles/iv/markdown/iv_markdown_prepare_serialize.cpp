/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_serialize.h"

#include "base/variant.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"

#include "styles/style_iv.h"

#include <QtCore/QByteArray>

#include <limits>
#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] QString EncodeInlineTextObjectBytes(const QByteArray &value) {
	return QString::fromUtf8(value.toPercentEncoding());
}

[[nodiscard]] QByteArray DecodeInlineTextObjectBytes(QStringView value) {
	return QByteArray::fromPercentEncoding(value.toLatin1());
}

[[nodiscard]] QString EncodeInlineTextObjectField(const QString &value) {
	return EncodeInlineTextObjectBytes(value.toUtf8());
}

[[nodiscard]] QString DecodeInlineTextObjectField(QStringView value) {
	return QString::fromUtf8(DecodeInlineTextObjectBytes(value));
}

[[nodiscard]] QString RichButtonLabelEntityName(EntityType type) {
	return (type == EntityType::CustomEmoji)
		? u"custom-emoji"_q
		: (type == EntityType::FormattedDate)
		? u"formatted-date"_q
		: QString();
}

[[nodiscard]] EntityType RichButtonLabelEntityType(QStringView name) {
	return (name == u"custom-emoji"_q)
		? EntityType::CustomEmoji
		: (name == u"formatted-date"_q)
		? EntityType::FormattedDate
		: EntityType::Invalid;
}

[[nodiscard]] QString SerializeRichButtonLabel(const TextWithEntities &text) {
	auto result = EncodeInlineTextObjectField(text.text);
	for (const auto &entity : text.entities) {
		const auto name = RichButtonLabelEntityName(entity.type());
		if (name.isEmpty()) {
			continue;
		}
		result += u"|"_q
			+ name
			+ u","_q
			+ QString::number(entity.offset())
			+ u","_q
			+ QString::number(entity.length())
			+ u","_q
			+ EncodeInlineTextObjectField(entity.data());
	}
	return result;
}

[[nodiscard]] TextWithEntities ParseRichButtonLabel(QStringView data) {
	const auto parts = data.split(QChar('|'), Qt::KeepEmptyParts);
	if (parts.empty()) {
		return {};
	}
	auto result = TextWithEntities{
		DecodeInlineTextObjectField(parts[0]),
	};
	const auto size = int(result.text.size());
	for (auto i = 1, count = int(parts.size()); i != count; ++i) {
		const auto fields = parts[i].split(QChar(','), Qt::KeepEmptyParts);
		if (fields.size() != 4) {
			continue;
		}
		auto offsetOk = false;
		auto lengthOk = false;
		const auto type = RichButtonLabelEntityType(fields[0]);
		const auto offset = fields[1].toInt(&offsetOk);
		const auto length = fields[2].toInt(&lengthOk);
		if (type == EntityType::Invalid
			|| !offsetOk
			|| !lengthOk
			|| offset < 0
			|| length <= 0
			|| offset + length > size) {
			continue;
		}
		result.entities.push_back(EntityInText(
			type,
			offset,
			length,
			DecodeInlineTextObjectField(fields[3])));
	}
	return result;
}

[[nodiscard]] std::optional<InlineTextObjectButtonData> InlineLinkButtonFor(
		const EntityInText &entity) {
	if (entity.type() != EntityType::CustomEmoji) {
		return std::nullopt;
	}
	const auto parsed = ParseInlineTextObjectEntity(entity.data());
	if (!parsed) {
		return std::nullopt;
	}
	const auto button = std::get_if<InlineTextObjectButtonData>(&parsed->data);
	return (button && button->link)
		? std::make_optional(*button)
		: std::nullopt;
}

void ReplaceInlineObjectText(
		TextWithEntities *text,
		const EntityInText &object,
		const QString &replacement) {
	const auto offset = object.offset();
	const auto delta = int(replacement.size()) - object.length();
	text->text.replace(offset, object.length(), replacement);
	for (auto &entity : text->entities) {
		if (&entity == &object) {
			continue;
		} else if (entity.offset() > offset) {
			entity.shiftRight(delta);
		} else if (entity.offset() + entity.length() > offset) {
			entity.shrinkFromRight(-delta);
		}
	}
}

[[nodiscard]] EntitiesInText::iterator ExpandInlineObjectEntities(
		TextWithEntities *text,
		EntitiesInText::iterator i,
		const EntitiesInText &nested) {
	auto &entities = text->entities;
	const auto offset = i->offset();
	auto at = int(i - entities.begin());
	entities.erase(i);
	for (const auto &entity : nested) {
		entities.insert(at++, EntityInText(
			entity.type(),
			entity.offset() + offset,
			entity.length(),
			entity.data()));
	}
	return entities.begin() + at;
}

[[nodiscard]] int TextSizeForFormula(const style::TextStyle &textStyle) {
	return std::max(textStyle.font->height, 1);
}

} // namespace

QString SerializeInlineTextObjectEntity(const InlineTextObjectEntity &object) {
	switch (object.kind) {
	case InlineTextObjectKind::Formula: {
		const auto data = std::get_if<InlineTextObjectFormulaData>(&object.data);
		if (!data) {
			return QString();
		}
		return u"iv-markdown:inline-text-object;formula;"_q
			+ EncodeInlineTextObjectField(data->copySource)
			+ u";"_q
			+ EncodeInlineTextObjectField(data->trimmedTex);
	} break;
	case InlineTextObjectKind::IvImage: {
		const auto data = std::get_if<InlineTextObjectIvImageData>(&object.data);
		if (!data) {
			return QString();
		}
		return u"iv-markdown:inline-text-object;iv-image;"_q
			+ QString::number(data->documentId)
			+ u";"_q
			+ QString::number(data->width)
			+ u";"_q
			+ QString::number(data->height)
			+ u";"_q
			+ EncodeInlineTextObjectField(data->replacementText);
	} break;
	case InlineTextObjectKind::Button: {
		const auto data = std::get_if<InlineTextObjectButtonData>(&object.data);
		if (!data) {
			return QString();
		}
		const auto label = SerializeRichButtonLabel(data->label);
		return u"iv-markdown:inline-text-object;button;"_q
			+ EncodeInlineTextObjectField(label)
			+ u";"_q
			+ QString::number(int(data->color))
			+ u";"_q
			+ QString::number(int(data->type))
			+ u";"_q
			+ QString::number(data->link ? 1 : 0)
			+ u";"_q
			+ EncodeInlineTextObjectBytes(data->data)
			+ u";"_q
			+ QString::number(data->buttonId)
			+ u";"_q
			+ QString::number(data->peerTypes.value());
	} break;
	}
	return QString();
}

std::optional<InlineTextObjectEntity> ParseInlineTextObjectEntity(
		QStringView data) {
	const auto parts = data.split(QChar(';'), Qt::KeepEmptyParts);
	if (parts.size() < 2
		|| parts[0] != u"iv-markdown:inline-text-object"_q) {
		return std::nullopt;
	}
	if (parts[1] == u"formula"_q) {
		if (parts.size() != 4) {
			return std::nullopt;
		}
		return InlineTextObjectEntity{
			.kind = InlineTextObjectKind::Formula,
			.data = InlineTextObjectFormulaData{
				.copySource = DecodeInlineTextObjectField(parts[2]),
				.trimmedTex = DecodeInlineTextObjectField(parts[3]),
			},
		};
	} else if (parts[1] == u"iv-image"_q) {
		if (parts.size() != 6) {
			return std::nullopt;
		}
		auto documentIdOk = false;
		auto widthOk = false;
		auto heightOk = false;
		const auto documentId = parts[2].toULongLong(&documentIdOk);
		const auto width = parts[3].toInt(&widthOk);
		const auto height = parts[4].toInt(&heightOk);
		if (!documentIdOk || !widthOk || !heightOk) {
			return std::nullopt;
		}
		return InlineTextObjectEntity{
			.kind = InlineTextObjectKind::IvImage,
			.data = InlineTextObjectIvImageData{
				.documentId = documentId,
				.width = width,
				.height = height,
				.replacementText = DecodeInlineTextObjectField(parts[5]),
			},
		};
	} else if (parts[1] == u"button"_q) {
		if (parts.size() != 9) {
			return std::nullopt;
		}
		using Color = HistoryMessageMarkupButton::Color;
		using Type = HistoryMessageMarkupButton::Type;
		using PeerTypes = InlineBots::PeerTypes;
		auto colorOk = false;
		auto typeOk = false;
		auto buttonIdOk = false;
		auto peerTypesOk = false;
		const auto color = parts[3].toInt(&colorOk);
		const auto type = parts[4].toInt(&typeOk);
		const auto buttonId = parts[7].toLongLong(&buttonIdOk);
		const auto peerTypes = parts[8].toUInt(&peerTypesOk);
		if (!colorOk
			|| color < 0
			|| color > int(Color::Success)
			|| !typeOk
			|| type < 0
			|| type >= int(Type::kCount)
			|| !buttonIdOk
			|| !peerTypesOk
			|| peerTypes > uint(
				std::numeric_limits<PeerTypes::Type>::max())) {
			return std::nullopt;
		}
		const auto label = DecodeInlineTextObjectField(parts[2]);
		return InlineTextObjectEntity{
			.kind = InlineTextObjectKind::Button,
			.data = InlineTextObjectButtonData{
				.label = ParseRichButtonLabel(label),
				.data = DecodeInlineTextObjectBytes(parts[6]),
				.buttonId = buttonId,
				.type = Type(type),
				.color = Color(color),
				.peerTypes = PeerTypes::from_raw(PeerTypes::Type(peerTypes)),
				.link = (parts[5] == u"1"_q),
			},
		};
	}
	return std::nullopt;
}

TextWithEntities ResolveRichButtonLabelDates(
		TextWithEntities label,
		const Ui::Text::FormattedDateFactory &dates) {
	// BlockParser::checkEntities hands a FormattedDate entity its own internal
	// index, createBlock promotes that index to the block's link index, and
	// the renderer then paints such a block with the link pen and with an
	// underlined font, and publishes a FormattedDateClickHandler for it. None
	// of that belongs to a button label, which reads as one label in one font,
	// one pen and one click target, so the date is resolved here exactly the
	// way the parser would resolve it and the entity is dropped before layout.
	// The list is searched from the front again after every replacement,
	// because a normalized label orders its entities as a laminar family in
	// post-order rather than by offset, and a textDate may itself contain a
	// textCustomEmoji, whose entry the rewrite drops together with the text it
	// covered — which is what the parser does today when it jumps past the
	// date it substituted.
	while (true) {
		const auto i = ranges::find_if(label.entities, [](
				const EntityInText &entity) {
			return (entity.type() == EntityType::FormattedDate);
		});
		if (i == label.entities.end()) {
			return label;
		}
		const auto offset = i->offset();
		const auto length = i->length();
		const auto till = offset + length;
		const auto [date, flags] = DeserializeFormattedDateData(i->data());
		if ((flags == FormattedDateFlags()) || !dates) {
			label.entities.erase(i);
			continue;
		}
		const auto replacement = dates(date, flags).text;
		const auto delta = int(replacement.size()) - length;
		label.text.replace(offset, length, replacement);
		auto entities = EntitiesInText();
		entities.reserve(label.entities.size());
		for (const auto &entity : label.entities) {
			if (&entity == &*i) {
				continue;
			}
			auto updated = entity;
			if (entity.offset() >= till) {
				updated.shiftRight(delta);
			} else if (entity.offset() + entity.length() <= offset) {
			} else if ((entity.offset() <= offset)
				&& (entity.offset() + entity.length() >= till)) {
				updated.shrinkFromRight(-delta);
			} else {
				continue;
			}
			entities.push_back(updated);
		}
		label.entities = std::move(entities);
	}
}

void ExpandInlineTextObjects(TextWithEntities *text, bool withIcons) {
	auto &entities = text->entities;
	for (auto i = entities.begin(); i != entities.end();) {
		if (i->type() != EntityType::CustomEmoji) {
			++i;
			continue;
		}
		const auto object = ParseInlineTextObjectEntity(i->data());
		if (!object) {
			++i;
			continue;
		}
		auto nested = EntitiesInText();
		const auto replacement = v::match(object->data, [](
				const InlineTextObjectFormulaData &data) {
			return data.trimmedTex;
		}, [](const InlineTextObjectIvImageData &data) {
			return data.replacementText;
		}, [&](const InlineTextObjectButtonData &data) {
			nested = data.label.entities;
			return data.label.text;
		});
		const auto offset = i->offset();
		ReplaceInlineObjectText(text, *i, replacement);
		const auto formula = (object->kind
			== InlineTextObjectKind::Formula);
		if (withIcons && formula && !replacement.isEmpty()) {
			const auto icon = Ui::Text::IconEmoji(
				&st::ivSummaryMathIcon,
				replacement);
			*i = EntityInText(
				EntityType::CustomEmoji,
				offset,
				int(replacement.size()),
				icon.entities.front().data());
			++i;
		} else {
			i = ExpandInlineObjectEntities(text, i, nested);
		}
	}
}

bool TextHasInlineLinkButton(const TextWithEntities &text) {
	return ranges::any_of(text.entities, [](const EntityInText &entity) {
		return InlineLinkButtonFor(entity).has_value();
	});
}

std::vector<InlineLinkButtonSpan> ExpandInlineLinkButtons(
		TextWithEntities *text,
		RichButtonLabelDates dates,
		const Ui::Text::FormattedDateFactory &factory) {
	auto result = std::vector<InlineLinkButtonSpan>();
	auto &entities = text->entities;
	for (auto i = entities.begin(); i != entities.end();) {
		const auto button = InlineLinkButtonFor(*i);
		if (!button) {
			++i;
			continue;
		}
		auto label = (dates == RichButtonLabelDates::Resolve)
			? ResolveRichButtonLabelDates(button->label, factory)
			: button->label;
		const auto actionable = (button->type
			!= HistoryMessageMarkupButton::Type::Disabled);
		auto data = actionable ? i->data() : QString();
		const auto offset = i->offset();
		ReplaceInlineObjectText(text, *i, label.text);
		i = ExpandInlineObjectEntities(text, i, label.entities);
		if (actionable && !label.text.isEmpty()) {
			result.push_back({
				.offset = offset,
				.length = int(label.text.size()),
				.data = std::move(data),
			});
		}
	}
	return result;
}

TextWithEntities NormalizeRichButtonLabel(TextWithEntities text) {
	ExpandInlineTextObjects(&text, false);
	text.entities.erase(
		ranges::remove_if(text.entities, [](const EntityInText &entity) {
			const auto type = entity.type();
			return (type != EntityType::CustomEmoji)
				&& (type != EntityType::FormattedDate);
		}),
		text.entities.end());
	for (auto i = text.entities.begin(); i != text.entities.end();) {
		if (i->type() != EntityType::FormattedDate) {
			++i;
			continue;
		}
		auto [date, flags] = DeserializeFormattedDateData(i->data());
		if (date <= 0
			&& i->data().startsWith(Ui::InputField::kCustomDateTagStart)) {
			date = int32(i->data().mid(
				Ui::InputField::kCustomDateTagStart.size()).toInt());
		}
		if (date <= 0) {
			i = text.entities.erase(i);
			continue;
		}
		if (!flags) {
			flags |= FormattedDateFlag::ShortDate;
		}
		*i = EntityInText(
			EntityType::FormattedDate,
			i->offset(),
			i->length(),
			SerializeFormattedDateData(date, flags));
		++i;
	}
	TextUtilities::Trim(text);
	return text;
}

QString InlineFormulaCopySource(const QString &source) {
	return u"$"_q + source + u"$"_q;
}

MarkdownPrepareDimensions CaptureMarkdownPrepareDimensions() {
	return CaptureMarkdownPrepareDimensions(st::defaultMarkdown);
}

MarkdownPrepareDimensions CaptureMarkdownPrepareDimensions(
		const style::Markdown &st) {
	auto result = MarkdownPrepareDimensions();
	result.bodyTextSize = TextSizeForFormula(st.body);
	result.headingTextSizes = {
		TextSizeForFormula(st.heading1),
		TextSizeForFormula(st.heading2),
		TextSizeForFormula(st.heading3),
		TextSizeForFormula(st.heading4),
		TextSizeForFormula(st.heading5),
		TextSizeForFormula(st.heading6),
	};
	result.tableHeaderTextSize = TextSizeForFormula(
		st.table.headerStyle);
	result.tableBodyTextSize = TextSizeForFormula(
		st.table.bodyStyle);
	result.displayMathTextSize = st.displayMath.textSize;
	result.displayMathMaxRenderWidth = st.displayMath.maxRenderWidth;
	result.displayMathMaxRenderHeight = st.displayMath.maxRenderHeight;
	return result;
}

} // namespace Iv::Markdown

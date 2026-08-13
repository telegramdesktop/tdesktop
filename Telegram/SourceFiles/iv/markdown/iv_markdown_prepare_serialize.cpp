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
			|| type > int(Type::CreateBot)
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
		const auto length = i->length();
		const auto delta = int(replacement.size()) - length;
		text->text.replace(offset, length, replacement);
		for (auto &entity : entities) {
			if (&entity == &*i) {
				continue;
			} else if (entity.offset() > offset) {
				entity.shiftRight(delta);
			} else if (entity.offset() + entity.length() > offset) {
				entity.shrinkFromRight(-delta);
			}
		}
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
			auto at = int(i - entities.begin());
			i = entities.erase(i);
			for (const auto &entity : nested) {
				entities.insert(at++, EntityInText(
					entity.type(),
					entity.offset() + offset,
					entity.length(),
					entity.data()));
			}
			i = entities.begin() + at;
		}
	}
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

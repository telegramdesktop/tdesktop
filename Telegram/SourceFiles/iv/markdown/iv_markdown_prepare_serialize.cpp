/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_serialize.h"

#include "base/variant.h"
#include "ui/text/text_utilities.h"

#include "styles/style_iv.h"

#include <QtCore/QByteArray>

#include <utility>

namespace Iv::Markdown {
namespace {

[[nodiscard]] QString EncodeInlineTextObjectField(const QString &value) {
	return QString::fromUtf8(value.toUtf8().toPercentEncoding());
}

[[nodiscard]] QString DecodeInlineTextObjectField(QStringView value) {
	return QString::fromUtf8(
		QByteArray::fromPercentEncoding(value.toLatin1()));
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
		const auto replacement = v::match(object->data, [](
				const InlineTextObjectFormulaData &data) {
			return data.trimmedTex;
		}, [](const InlineTextObjectIvImageData &data) {
			return data.replacementText;
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
			i = entities.erase(i);
		}
	}
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

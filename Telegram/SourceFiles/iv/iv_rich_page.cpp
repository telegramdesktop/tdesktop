/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_rich_page.h"

#include "base/algorithm.h"
#include "base/flat_map.h"
#include "base/qthelp_url.h"
#include "base/unixtime.h"
#include "base/variant.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "iv/markdown/iv_markdown_prepare.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"
#include "styles/style_iv.h"

#include <algorithm>
#include <limits>

#include <QtCore/QSize>

namespace Iv {
namespace {

using Block = RichPage::Block;
using BlockKind = RichPage::BlockKind;
using GroupedMediaIntent = RichPage::GroupedMediaIntent;
using ListItem = RichPage::ListItem;
using ListKind = RichPage::ListKind;
using OrderedListData = RichPage::OrderedListData;
using OrderedListItemData = RichPage::OrderedListItemData;
using RelatedArticle = RichPage::RelatedArticle;
using RichText = RichPage::RichText;
using TableAlignment = RichPage::TableAlignment;
using TableCell = RichPage::TableCell;
using TableRow = RichPage::TableRow;
using TableVerticalAlignment = RichPage::TableVerticalAlignment;
using TaskState = RichPage::TaskState;

[[nodiscard]] QString FormulaTexFromSource(QString source) {
	source = source.trimmed();
	if (source.size() >= 2
		&& source.front() == QChar('$')
		&& source.back() == QChar('$')) {
		source = source.mid(1, source.size() - 2).trimmed();
	}
	return source;
}

enum class OrderedMarkerType {
	Decimal,
	LowerAlpha,
	UpperAlpha,
	LowerRoman,
	UpperRoman,
};

[[nodiscard]] OrderedMarkerType ResolveOrderedMarkerType(
		const std::optional<QString> &type) {
	if (!type.has_value()) {
		return OrderedMarkerType::Decimal;
	}
	const auto &value = *type;
	if (value.compare(u"a"_q, Qt::CaseInsensitive) == 0
		|| value.compare(u"lower-alpha"_q, Qt::CaseInsensitive) == 0
		|| value.compare(u"lower-latin"_q, Qt::CaseInsensitive) == 0) {
		return OrderedMarkerType::LowerAlpha;
	} else if (value == u"A"_q
		|| value.compare(u"upper-alpha"_q, Qt::CaseInsensitive) == 0
		|| value.compare(u"upper-latin"_q, Qt::CaseInsensitive) == 0) {
		return OrderedMarkerType::UpperAlpha;
	} else if (value == u"i"_q
		|| value.compare(u"lower-roman"_q, Qt::CaseInsensitive) == 0) {
		return OrderedMarkerType::LowerRoman;
	} else if (value == u"I"_q
		|| value.compare(u"upper-roman"_q, Qt::CaseInsensitive) == 0) {
		return OrderedMarkerType::UpperRoman;
	}
	return OrderedMarkerType::Decimal;
}

[[nodiscard]] QString OrderedAlphaText(int value, bool upper) {
	if (value <= 0) {
		return QString::number(value);
	}
	auto result = QString();
	while (value > 0) {
		--value;
		result.prepend(QChar((upper ? 'A' : 'a') + (value % 26)));
		value /= 26;
	}
	return result;
}

[[nodiscard]] QString OrderedRomanText(int value, bool upper) {
	if (value <= 0) {
		return QString::number(value);
	}
	struct RomanPart {
		int value = 0;
		const char *text = nullptr;
	};
	static constexpr RomanPart kParts[] = {
		RomanPart{ 1000, "M" },
		RomanPart{ 900, "CM" },
		RomanPart{ 500, "D" },
		RomanPart{ 400, "CD" },
		RomanPart{ 100, "C" },
		RomanPart{ 90, "XC" },
		RomanPart{ 50, "L" },
		RomanPart{ 40, "XL" },
		RomanPart{ 10, "X" },
		RomanPart{ 9, "IX" },
		RomanPart{ 5, "V" },
		RomanPart{ 4, "IV" },
		RomanPart{ 1, "I" },
	};
	auto result = QString();
	for (const auto &part : kParts) {
		while (value >= part.value) {
			result += QString::fromLatin1(part.text);
			value -= part.value;
		}
	}
	return upper ? result : result.toLower();
}

[[nodiscard]] QString OrderedMarkerBody(
		int value,
		const std::optional<QString> &type) {
	switch (ResolveOrderedMarkerType(type)) {
	case OrderedMarkerType::LowerAlpha:
		return OrderedAlphaText(value, false);
	case OrderedMarkerType::UpperAlpha:
		return OrderedAlphaText(value, true);
	case OrderedMarkerType::LowerRoman:
		return OrderedRomanText(value, false);
	case OrderedMarkerType::UpperRoman:
		return OrderedRomanText(value, true);
	case OrderedMarkerType::Decimal:
		return QString::number(value);
	}
	return QString::number(value);
}

[[nodiscard]] QString OrderedRawMarkerText(const QString &raw) {
	if (raw.isEmpty() || raw.endsWith('.') || raw.endsWith(')')) {
		return raw;
	}
	return raw + u"."_q;
}

[[nodiscard]] QString OrderedMarkerText(
		const OrderedListData &list,
		const OrderedListItemData &item,
		int fallbackValue) {
	if (item.hasRawText()) {
		return OrderedRawMarkerText(item.rawText());
	}
	const auto type = item.type.has_value() ? item.type : list.type;
	const auto value = item.value.value_or(fallbackValue);
	return OrderedMarkerBody(value, type) + u"."_q;
}

[[nodiscard]] int OrderedListSequenceStart(const Block &block) {
	if (block.orderedList.start.has_value()) {
		return *block.orderedList.start;
	}
	return block.orderedList.reversed
		? int(block.listItems.size())
		: 1;
}

const auto PhotoLargeLevels = u"ydxcwmbsa"_q;
constexpr auto kDefaultMapWidth = 400;
constexpr auto kDefaultMapHeight = 200;

[[nodiscard]] int NonZeroMapWidth(int width) {
	return (width > 0) ? width : kDefaultMapWidth;
}

[[nodiscard]] int NonZeroMapHeight(int height) {
	return (height > 0) ? height : kDefaultMapHeight;
}

[[nodiscard]] Block MakeBlock(BlockKind kind) {
	auto result = Block();
	result.kind = kind;
	return result;
}

[[nodiscard]] Block MakeHeadingBlock(int level) {
	auto result = MakeBlock(BlockKind::Heading);
	result.headingLevel = level;
	return result;
}

enum class ParseSource {
	InstantViewPage,
	RichMessage,
};

struct ParseContext {
	ParseContext(
		not_null<Main::Session*> session,
		ParseSource source = ParseSource::InstantViewPage)
	: session(session)
	, source(source) {
	}

	not_null<Main::Session*> session;
	ParseSource source = ParseSource::InstantViewPage;
	base::flat_map<uint64, PhotoData*> photos;
	base::flat_map<uint64, DocumentData*> documents;
	base::flat_map<uint64, QSize> photoSizes;
	struct DocumentInfo {
		int width = 0;
		int height = 0;
		QString fileName;
		QString title;
		QString performer;
		int duration = 0;
		bool isVideoFile = false;
		bool isAnimation = false;
	};
	base::flat_map<uint64, DocumentInfo> documentInfos;
	bool dropRichTextClickHandlers = false;
	bool displayTextDiff = false;
};

struct RichMessageMetrics {
	int textLength = 0;
	int blockCount = 0;
	int maxDepth = 0;
	int mediaCount = 0;
	int maxTableColumns = 0;
	int tableColumnMeasurementLimit = 0;
};

using TableOccupancyRow = std::vector<char>;
using TableOccupancyGrid = std::vector<TableOccupancyRow>;

enum class RichTextParseMode {
	Normal,
	DropClickHandlers,
};

void AccumulateTextLength(
		RichMessageMetrics *metrics,
		const RichText &text) {
	metrics->textLength += int(text.text.text.size());
}

void AccumulateTextLength(
		RichMessageMetrics *metrics,
		const QString &text) {
	metrics->textLength += int(text.size());
}

[[nodiscard]] bool IsMediaKind(BlockKind kind) {
	switch (kind) {
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] int NormalizeTableSpan(int span) {
	return std::max(span, 1);
}

[[nodiscard]] int ClampTableRowspan(
		int rawRowspan,
		int row,
		int rowCount) {
	if ((row < 0) || (row >= rowCount) || (rowCount <= 0)) {
		return 0;
	}
	return std::min(NormalizeTableSpan(rawRowspan), rowCount - row);
}

[[nodiscard]] int ClampTableColspan(
		int rawColspan,
		int column,
		int maxColumns) {
	if ((column < 0) || (column >= maxColumns) || (maxColumns <= 0)) {
		return 0;
	}
	return std::min(NormalizeTableSpan(rawColspan), maxColumns - column);
}

[[nodiscard]] bool CanOccupyTableSlots(
		const TableOccupancyGrid &occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy.size()))) {
		return false;
	}
	const auto rowLimit = std::min(row + rowspan, int(occupancy.size()));
	const auto columnLimit = column + colspan;
	for (auto currentRow = row; currentRow != rowLimit; ++currentRow) {
		const auto &occupied = occupancy[currentRow];
		const auto occupiedLimit = std::min(columnLimit, int(occupied.size()));
		for (auto currentColumn = column;
				currentColumn < occupiedLimit;
				++currentColumn) {
			if (occupied[currentColumn]) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] int FirstAvailableTableColumn(
		const TableOccupancyGrid &occupancy,
		int row,
		int rowspan,
		int colspan,
		int maxColumns) {
	if ((row < 0)
		|| (row >= int(occupancy.size()))
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (maxColumns <= 0)) {
		return -1;
	}
	for (auto column = 0; column != maxColumns; ++column) {
		const auto effectiveColspan = ClampTableColspan(
			colspan,
			column,
			maxColumns);
		if (effectiveColspan <= 0) {
			continue;
		}
		if (CanOccupyTableSlots(
				occupancy,
				row,
				column,
				rowspan,
				effectiveColspan)) {
			return column;
		}
	}
	return -1;
}

void MarkTableSlots(
		TableOccupancyGrid *occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy->size()))) {
		return;
	}
	const auto rowLimit = std::min(row + rowspan, int(occupancy->size()));
	const auto columnLimit = column + colspan;
	for (auto currentRow = row; currentRow != rowLimit; ++currentRow) {
		auto &occupied = (*occupancy)[currentRow];
		if (columnLimit > int(occupied.size())) {
			occupied.resize(columnLimit, false);
		}
		for (auto currentColumn = column;
				currentColumn != columnLimit;
				++currentColumn) {
			occupied[currentColumn] = true;
		}
	}
}

[[nodiscard]] int TableColumnCount(const TableOccupancyGrid &occupancy) {
	auto result = 0;
	for (const auto &row : occupancy) {
		result = std::max(result, int(row.size()));
	}
	return result;
}

[[nodiscard]] int TableColumnMeasurementLimit(int maxTableCols) {
	if (maxTableCols <= 0) {
		return 1;
	}
	return (maxTableCols == std::numeric_limits<int>::max())
		? maxTableCols
		: (maxTableCols + 1);
}

[[nodiscard]] int EffectiveTableColumns(
		const std::vector<TableRow> &rows,
		int maxColumns) {
	if (rows.empty() || maxColumns <= 0) {
		return 0;
	}
	auto occupancy = TableOccupancyGrid(rows.size());
	for (auto rowIndex = 0; rowIndex != int(rows.size()); ++rowIndex) {
		const auto &row = rows[rowIndex];
		for (const auto &cell : row.cells) {
			const auto normalizedColspan = NormalizeTableSpan(cell.colspan);
			const auto rowspan = ClampTableRowspan(
				cell.rowspan,
				rowIndex,
				int(rows.size()));
			if (rowspan <= 0) {
				continue;
			}
			const auto column = FirstAvailableTableColumn(
				occupancy,
				rowIndex,
				rowspan,
				normalizedColspan,
				maxColumns);
			if (column < 0) {
				return maxColumns;
			}
			const auto colspan = ClampTableColspan(
				normalizedColspan,
				column,
				maxColumns);
			if (colspan != normalizedColspan) {
				return maxColumns;
			}
			MarkTableSlots(
				&occupancy,
				rowIndex,
				column,
				rowspan,
				colspan);
		}
	}
	return TableColumnCount(occupancy);
}

void AccumulateBlockMetrics(
		RichMessageMetrics *metrics,
		const std::vector<Block> &blocks,
		int depth);

void AccumulateBlockMetrics(
		RichMessageMetrics *metrics,
		const Block &block,
		int depth) {
	++metrics->blockCount;
	metrics->maxDepth = std::max(metrics->maxDepth, depth);
	AccumulateTextLength(metrics, block.text);
	AccumulateTextLength(metrics, block.caption);
	AccumulateTextLength(metrics, block.formula);
	if (IsMediaKind(block.kind)) {
		++metrics->mediaCount;
	}
	for (const auto &child : block.blocks) {
		AccumulateBlockMetrics(metrics, child, depth + 1);
	}
	for (const auto &item : block.listItems) {
		++metrics->blockCount;
		AccumulateTextLength(metrics, item.text);
		AccumulateBlockMetrics(metrics, item.blocks, depth + 1);
	}
	for (const auto &item : block.mediaItems) {
		if (IsMediaKind(item.kind)) {
			++metrics->mediaCount;
		}
	}
	for (const auto &row : block.tableRows) {
		++metrics->blockCount;
		for (const auto &cell : row.cells) {
			AccumulateTextLength(metrics, cell.text);
		}
	}
	metrics->maxTableColumns = std::max(
		metrics->maxTableColumns,
		EffectiveTableColumns(
			block.tableRows,
			metrics->tableColumnMeasurementLimit));
}

void AccumulateBlockMetrics(
		RichMessageMetrics *metrics,
		const std::vector<Block> &blocks,
		int depth) {
	for (const auto &block : blocks) {
		AccumulateBlockMetrics(metrics, block, depth);
	}
}

[[nodiscard]] RichMessageMetrics ComputeRichMessageMetrics(
		const RichPage &page,
		const RichMessageLimits &limits) {
	auto result = RichMessageMetrics();
	result.tableColumnMeasurementLimit = TableColumnMeasurementLimit(
		limits.maxTableCols);
	AccumulateBlockMetrics(&result, page.blocks, 1);
	return result;
}

[[nodiscard]] QString DateText(TimeId date) {
	return langDateTimeFull(base::unixtime::parse(date));
}

[[nodiscard]] bool AddEntity(
		TextWithEntities *text,
		int from,
		EntityType type,
		const QString &data = QString()) {
	const auto length = text->text.size() - from;
	if (length <= 0) {
		return true;
	}
	text->entities.push_back(EntityInText(type, from, length, data));
	return true;
}

void AddRichAnchor(
		QString *primary,
		std::vector<QString> *extra,
		QString anchorId) {
	if (anchorId.isEmpty()) {
		return;
	}
	if (primary && primary->isEmpty()) {
		*primary = std::move(anchorId);
		return;
	}
	if (primary && *primary == anchorId) {
		return;
	}
	if (extra && !ranges::contains(*extra, anchorId)) {
		extra->push_back(std::move(anchorId));
	}
}

void AppendRich(RichText *to, RichText &&from) {
	to->text.append(std::move(from.text));
	AddRichAnchor(&to->anchorId, &to->anchorIds, std::move(from.anchorId));
	for (auto &anchorId : from.anchorIds) {
		AddRichAnchor(&to->anchorId, &to->anchorIds, std::move(anchorId));
	}
}

[[nodiscard]] QString RichPageLinkEntityData(
		ParseSource source,
		const QString &url,
		uint64 webpageId) {
	return (source == ParseSource::InstantViewPage
		&& webpageId
		&& !url.isEmpty())
		? EncodeRichPageLinkUrl(url, webpageId)
		: url;
}

[[nodiscard]] QString MentionNameEntityData(
		not_null<Main::Session*> session,
		uint64 userId) {
	if (userId == 0) {
		return QString();
	}
	const auto loaded = session->data().userLoaded(UserId(userId));
	return TextUtilities::MentionNameDataFromFields({
		.selfId = session->userId().bare,
		.userId = userId,
		.accessHash = loaded ? loaded->accessHash() : 0,
	});
}

[[nodiscard]] TaskState TaskStateFromFlags(
		bool checkbox,
		bool checked) {
	if (!checkbox) {
		return TaskState::None;
	}
	return checked ? TaskState::Checked : TaskState::Unchecked;
}

[[nodiscard]] uint64 PhotoIdFromMtp(const MTPPhoto &photo) {
	return photo.match([](const auto &data) {
		return uint64(data.vid().v);
	});
}

[[nodiscard]] uint64 DocumentIdFromMtp(const MTPDocument &document) {
	return document.match([](const auto &data) {
		return uint64(data.vid().v);
	});
}

[[nodiscard]] QSize PhotoSizeFromMtp(const MTPPhoto &photo) {
	auto result = QSize();
	photo.match([](const MTPDphotoEmpty &) {
	}, [&](const MTPDphoto &data) {
		auto bestLevel = PhotoLargeLevels.size();
		const auto assign = [&](const QString &type, int width, int height) {
			const auto level = type.isEmpty()
				? -1
				: PhotoLargeLevels.indexOf(type.front());
			if (level >= 0 && level < bestLevel) {
				bestLevel = level;
				result = QSize(width, height);
			}
		};
		for (const auto &size : data.vsizes().v) {
			size.match([](const MTPDphotoSizeEmpty &) {
			}, [&](const MTPDphotoSize &row) {
				assign(qs(row.vtype()), row.vw().v, row.vh().v);
			}, [&](const MTPDphotoCachedSize &row) {
				assign(qs(row.vtype()), row.vw().v, row.vh().v);
			}, [&](const MTPDphotoStrippedSize &) {
			}, [&](const MTPDphotoSizeProgressive &row) {
				assign(qs(row.vtype()), row.vw().v, row.vh().v);
			}, [&](const MTPDphotoPathSize &) {
			});
		}
	});
	return result;
}

[[nodiscard]] ParseContext::DocumentInfo DocumentInfoFromMtp(
		const MTPDocument &document) {
	auto result = ParseContext::DocumentInfo();
	document.match([](const MTPDdocumentEmpty &) {
	}, [&](const MTPDdocument &data) {
		for (const auto &attribute : data.vattributes().v) {
			attribute.match([&](const MTPDdocumentAttributeAudio &row) {
				result.duration = row.vduration().v;
				result.title = qs(row.vtitle().value_or_empty());
				result.performer = qs(row.vperformer().value_or_empty());
			}, [&](const MTPDdocumentAttributeFilename &row) {
				result.fileName = qs(row.vfile_name());
			}, [&](const MTPDdocumentAttributeImageSize &row) {
				if (result.width <= 0 || result.height <= 0) {
					result.width = row.vw().v;
					result.height = row.vh().v;
				}
			}, [&](const MTPDdocumentAttributeAnimated &) {
				result.isAnimation = true;
			}, [&](const MTPDdocumentAttributeVideo &row) {
				result.isVideoFile = true;
				result.width = row.vw().v;
				result.height = row.vh().v;
			}, [&](const auto &) {
			});
		}
	});
	return result;
}

void MergeDocumentInfo(
		ParseContext::DocumentInfo *existing,
		ParseContext::DocumentInfo info) {
	if (!existing) {
		return;
	}
	if (existing->width <= 0 && info.width > 0) {
		existing->width = info.width;
	}
	if (existing->height <= 0 && info.height > 0) {
		existing->height = info.height;
	}
	if (existing->fileName.isEmpty() && !info.fileName.isEmpty()) {
		existing->fileName = std::move(info.fileName);
	}
	if (existing->title.isEmpty() && !info.title.isEmpty()) {
		existing->title = std::move(info.title);
	}
	if (existing->performer.isEmpty() && !info.performer.isEmpty()) {
		existing->performer = std::move(info.performer);
	}
	if (existing->duration <= 0 && info.duration > 0) {
		existing->duration = info.duration;
	}
	if (!existing->isVideoFile && info.isVideoFile) {
		existing->isVideoFile = true;
	}
	if (!existing->isAnimation && info.isAnimation) {
		existing->isAnimation = true;
	}
}

void RememberPhoto(ParseContext *context, const MTPPhoto &photo) {
	const auto id = PhotoIdFromMtp(photo);
	if (!id) {
		return;
	}
	context->photos[id] = context->session->data().processPhoto(photo).get();
	const auto size = PhotoSizeFromMtp(photo);
	const auto existing = context->photoSizes.find(id);
	if (!size.isEmpty() || existing == end(context->photoSizes)) {
		context->photoSizes[id] = size;
	}
}

void RememberDocument(ParseContext *context, const MTPDocument &document) {
	const auto id = DocumentIdFromMtp(document);
	if (!id) {
		return;
	}
	const auto processed = context->session->data().processDocument(document).get();
	context->documents[id] = processed;
	auto info = DocumentInfoFromMtp(document);
	const auto existing = context->documentInfos.find(id);
	if (existing != end(context->documentInfos)) {
		MergeDocumentInfo(&existing->second, std::move(info));
	} else {
		context->documentInfos.emplace(id, std::move(info));
	}
}

void RememberWebPageMedia(
		ParseContext *context,
		const MTPDwebPage &webpage) {
	if (const auto photo = webpage.vphoto()) {
		RememberPhoto(context, *photo);
	}
	if (const auto document = webpage.vdocument()) {
		RememberDocument(context, *document);
	}
}

[[nodiscard]] PhotoData *FindPhoto(
		const ParseContext &context,
		uint64 id) {
	const auto i = context.photos.find(id);
	return (i != end(context.photos)) ? i->second : nullptr;
}

[[nodiscard]] DocumentData *FindDocument(
		const ParseContext &context,
		uint64 id) {
	const auto i = context.documents.find(id);
	return (i != end(context.documents)) ? i->second : nullptr;
}

[[nodiscard]] QSize FindPhotoSize(
		const ParseContext &context,
		uint64 id) {
	const auto i = context.photoSizes.find(id);
	return (i != end(context.photoSizes)) ? i->second : QSize();
}

[[nodiscard]] ParseContext::DocumentInfo FindDocumentInfo(
		const ParseContext &context,
		uint64 id) {
	const auto i = context.documentInfos.find(id);
	return (i != end(context.documentInfos))
		? i->second
		: ParseContext::DocumentInfo();
}

[[nodiscard]] TableAlignment TableCellAlignment(
		const MTPDpageTableCell &data) {
	if (data.is_align_right()) {
		return TableAlignment::Right;
	} else if (data.is_align_center()) {
		return TableAlignment::Center;
	}
	return TableAlignment::Left;
}

[[nodiscard]] TableVerticalAlignment TableCellVerticalAlignment(
		const MTPDpageTableCell &data) {
	if (data.is_valign_bottom()) {
		return TableVerticalAlignment::Bottom;
	} else if (data.is_valign_middle()) {
		return TableVerticalAlignment::Middle;
	}
	return TableVerticalAlignment::Top;
}

[[nodiscard]] RichText ParseRichText(
		const MTPRichText &text,
		ParseContext *context,
		RichTextParseMode mode = RichTextParseMode::Normal);

[[nodiscard]] RichText ParseCaption(
		const MTPPageCaption &caption,
		ParseContext *context);

[[nodiscard]] bool AppendRichText(
		const MTPRichText &text,
		RichText *result,
		ParseContext *context,
		QString *anchorId,
		std::vector<QString> *anchorIds) {
	return text.match([&](const MTPDtextEmpty &) {
		return true;
	}, [&](const MTPDtextPlain &data) {
		result->text.append(qs(data.vtext()));
		return true;
	}, [&](const MTPDtextConcat &data) {
		for (const auto &part : data.vtexts().v) {
			if (!AppendRichText(part, result, context, anchorId, anchorIds)) {
				return false;
			}
		}
		return true;
	}, [&](const MTPDtextImage &data) {
		const auto replacementText = u"[image]"_q;
		if (context->source == ParseSource::RichMessage
			|| !data.vdocument_id().v
			|| data.vw().v <= 0
			|| data.vh().v <= 0) {
			result->text.append(replacementText);
			return true;
		}
		const auto entityData = Markdown::SerializeInlineTextObjectEntity({
			.kind = Markdown::InlineTextObjectKind::IvImage,
			.data = Markdown::InlineTextObjectIvImageData{
				.documentId = uint64(data.vdocument_id().v),
				.width = data.vw().v,
				.height = data.vh().v,
				.replacementText = replacementText,
			},
		});
		if (entityData.isEmpty()) {
			result->text.append(replacementText);
			return true;
		}
		const auto from = result->text.text.size();
		result->text.append(QChar::ObjectReplacementCharacter);
		result->text.entities.push_back(EntityInText(
			EntityType::CustomEmoji,
			from,
			1,
			entityData));
		return true;
	}, [&](const MTPDtextMath &data) {
		const auto source = FormulaTexFromSource(qs(data.vsource()));
		const auto entityData = Markdown::SerializeInlineTextObjectEntity({
			.kind = Markdown::InlineTextObjectKind::Formula,
			.data = Markdown::InlineTextObjectFormulaData{
				.copySource = Markdown::InlineFormulaCopySource(source),
				.trimmedTex = source,
			},
		});
		if (entityData.isEmpty()) {
			result->text.append(source);
			return true;
		}
		const auto from = result->text.text.size();
		result->text.append(QChar::ObjectReplacementCharacter);
		result->text.entities.push_back(EntityInText(
			EntityType::CustomEmoji,
			from,
			1,
			entityData));
		return true;
	}, [&](const MTPDtextCustomEmoji &data) {
		result->text.append(Ui::Text::SingleCustomEmoji(
			::Data::SerializeCustomEmojiId(uint64(data.vdocument_id().v)),
			qs(data.valt())));
		return true;
	}, [&](const MTPDtextBold &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Bold);
	}, [&](const MTPDtextItalic &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Italic);
	}, [&](const MTPDtextUnderline &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Underline);
	}, [&](const MTPDtextStrike &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::StrikeOut);
	}, [&](const MTPDtextFixed &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Code);
	}, [&](const MTPDtextUrl &data) {
		const auto from = result->text.text.size();
		if (!AppendRichText(data.vtext(), result, context, anchorId, anchorIds)) {
			return false;
		}
		const auto target = qs(data.vurl());
		if (result->text.text.size() == from) {
			result->text.append(target);
		}
		if (context->dropRichTextClickHandlers) {
			return true;
		}
		return AddEntity(
			&result->text,
			from,
			EntityType::CustomUrl,
			RichPageLinkEntityData(
				context->source,
				target,
				uint64(data.vwebpage_id().v)));
	}, [&](const MTPDtextEmail &data) {
		const auto from = result->text.text.size();
		if (!AppendRichText(data.vtext(), result, context, anchorId, anchorIds)) {
			return false;
		}
		const auto email = qs(data.vemail());
		if (result->text.text.size() == from) {
			result->text.append(email);
		}
		if (context->dropRichTextClickHandlers) {
			return true;
		}
		const auto target = u"mailto:"_q + email;
		return AddEntity(&result->text, from, EntityType::CustomUrl, target);
	}, [&](const MTPDtextSubscript &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Subscript);
	}, [&](const MTPDtextSuperscript &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Superscript);
	}, [&](const MTPDtextMarked &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Marked);
	}, [&](const MTPDtextSpoiler &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& AddEntity(&result->text, from, EntityType::Spoiler);
	}, [&](const MTPDtextMention &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::Mention));
	}, [&](const MTPDtextHashtag &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::Hashtag));
	}, [&](const MTPDtextBotCommand &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::BotCommand));
	}, [&](const MTPDtextCashtag &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::Cashtag));
	}, [&](const MTPDtextAutoUrl &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::Url));
	}, [&](const MTPDtextAutoEmail &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::Email));
	}, [&](const MTPDtextAutoPhone &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::Phone));
	}, [&](const MTPDtextBankCard &data) {
		const auto from = result->text.text.size();
		return AppendRichText(data.vtext(), result, context, anchorId, anchorIds)
			&& (context->dropRichTextClickHandlers
				|| AddEntity(&result->text, from, EntityType::BankCard));
	}, [&](const MTPDtextMentionName &data) {
		const auto from = result->text.text.size();
		if (!AppendRichText(data.vtext(), result, context, anchorId, anchorIds)) {
			return false;
		}
		if (context->dropRichTextClickHandlers) {
			return true;
		}
		const auto entityData = MentionNameEntityData(
			context->session,
			uint64(data.vuser_id().v));
		return entityData.isEmpty()
			? true
			: AddEntity(
				&result->text,
				from,
				EntityType::MentionName,
				entityData);
	}, [&](const MTPDtextDate &data) {
		const auto from = result->text.text.size();
		if (!AppendRichText(data.vtext(), result, context, anchorId, anchorIds)) {
			return false;
		}
		if (context->dropRichTextClickHandlers) {
			return true;
		}
		auto flags = FormattedDateFlags();
		if (data.is_relative()) {
			flags |= FormattedDateFlag::Relative;
		}
		if (data.is_short_time()) {
			flags |= FormattedDateFlag::ShortTime;
		}
		if (data.is_long_time()) {
			flags |= FormattedDateFlag::LongTime;
		}
		if (data.is_short_date()) {
			flags |= FormattedDateFlag::ShortDate;
		}
		if (data.is_long_date()) {
			flags |= FormattedDateFlag::LongDate;
		}
		if (data.is_day_of_week()) {
			flags |= FormattedDateFlag::DayOfWeek;
		}
		return AddEntity(
			&result->text,
			from,
			EntityType::FormattedDate,
			SerializeFormattedDateData(data.vdate().v, flags));
	}, [&](const MTPDtextPhone &data) {
		const auto from = result->text.text.size();
		if (!AppendRichText(data.vtext(), result, context, anchorId, anchorIds)) {
			return false;
		}
		const auto phone = qs(data.vphone());
		if (result->text.text.size() == from) {
			result->text.append(phone);
		}
		if (context->dropRichTextClickHandlers) {
			return true;
		}
		const auto target = u"tel:"_q + phone;
		return AddEntity(&result->text, from, EntityType::CustomUrl, target);
	}, [&](const MTPDtextAnchor &data) {
		AddRichAnchor(
			anchorId,
			anchorIds,
			Markdown::NormalizeFragmentId(qs(data.vname())));
		return AppendRichText(
			data.vtext(),
			result,
			context,
			anchorId,
			anchorIds);
	}, [&](const MTPDtextDiff &data) {
		if (!context->displayTextDiff) {
			return AppendRichText(
				data.vtext(),
				result,
				context,
				anchorId,
				anchorIds);
		}
		const auto deleted = result->text.text.size();
		if (!AppendRichText(
				data.vold_text(),
				result,
				context,
				anchorId,
				anchorIds)
			|| !AddEntity(&result->text, deleted, EntityType::StrikeOut)
			|| !AddEntity(
				&result->text,
				deleted,
				EntityType::Colorized,
				QString(QChar(kTextDiffDeletedColorIndex)))) {
			return false;
		}
		const auto inserted = result->text.text.size();
		return AppendRichText(
			data.vtext(),
			result,
			context,
			anchorId,
			anchorIds)
			&& AddEntity(&result->text, inserted, EntityType::Underline)
			&& AddEntity(
				&result->text,
				inserted,
				EntityType::Colorized,
				QString(QChar(kTextDiffInsertedColorIndex)));
	});
}

[[nodiscard]] RichText ParseRichText(
		const MTPRichText &text,
		ParseContext *context,
		RichTextParseMode mode) {
	auto result = RichText();
	auto anchorId = QString();
	auto anchorIds = std::vector<QString>();
	const auto wasDropClickHandlers = context->dropRichTextClickHandlers;
	context->dropRichTextClickHandlers
		= (mode == RichTextParseMode::DropClickHandlers);
	const auto parsed = AppendRichText(
		text,
		&result,
		context,
		&anchorId,
		&anchorIds);
	context->dropRichTextClickHandlers = wasDropClickHandlers;
	(void)parsed;
	result.anchorId = std::move(anchorId);
	result.anchorIds = std::move(anchorIds);
	return result;
}

[[nodiscard]] RichText ParseCaption(
		const MTPPageCaption &caption,
		ParseContext *context) {
	auto result = RichText();
	auto anchorId = QString();
	auto anchorIds = std::vector<QString>();
	(void)AppendRichText(
		caption.data().vtext(),
		&result,
		context,
		&anchorId,
		&anchorIds);
	auto credit = RichText();
	(void)AppendRichText(
		caption.data().vcredit(),
		&credit,
		context,
		&anchorId,
		&anchorIds);
	if (!credit.text.empty()) {
		if (!result.text.empty()) {
			result.text.append(QChar('\n'));
		}
		AppendRich(&result, std::move(credit));
	}
	result.anchorId = std::move(anchorId);
	result.anchorIds = std::move(anchorIds);
	return result;
}

void AdoptAnchor(QString *anchorId, RichText *text) {
	if (anchorId->isEmpty() && !text->anchorId.isEmpty()) {
		*anchorId = std::move(text->anchorId);
		text->anchorId.clear();
	}
}

void AdoptLeadingParagraphListItemText(ListItem *item) {
	// List items hold either inline text or a list of blocks, never both,
	// so adopt the paragraph text only if it is the single item block.
	if (item->blocks.size() != 1
		|| item->blocks.front().kind != BlockKind::Paragraph) {
		return;
	}
	item->text = std::move(item->blocks.front().text);
	item->anchorId = std::move(item->blocks.front().anchorId);
	item->blocks.erase(item->blocks.begin());
}

void AppendBlocks(
		const QVector<MTPPageBlock> &blocks,
		std::vector<Block> *result,
		ParseContext *context);

void AppendBlock(
		const MTPPageBlock &block,
		std::vector<Block> *result,
	ParseContext *context) {
	block.match([&](const MTPDpageBlockUnsupported &) {
		result->push_back(MakeBlock(BlockKind::Unsupported));
	}, [&](const MTPDpageBlockTitle &data) {
		auto parsed = MakeHeadingBlock(1);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockSubtitle &data) {
		auto parsed = MakeHeadingBlock(2);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockAuthorDate &data) {
		auto parsed = MakeBlock(BlockKind::AuthorDate);
		parsed.text = ParseRichText(data.vauthor(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		parsed.date = data.vpublished_date().v;
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockHeader &data) {
		auto parsed = MakeHeadingBlock(3);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockSubheader &data) {
		auto parsed = MakeHeadingBlock(4);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockParagraph &data) {
		auto parsed = MakeBlock(BlockKind::Paragraph);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockThinking &data) {
		auto parsed = MakeBlock(BlockKind::Thinking);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockPreformatted &data) {
		auto parsed = MakeBlock(BlockKind::Code);
		parsed.language = qs(data.vlanguage()).trimmed();
		parsed.text = ParseRichText(
			data.vtext(),
			context,
			RichTextParseMode::DropClickHandlers);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockFooter &data) {
		auto parsed = MakeBlock(BlockKind::Footer);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockDivider &) {
		result->push_back(MakeBlock(BlockKind::Divider));
	}, [&](const MTPDpageBlockAnchor &data) {
		const auto anchorId = Markdown::NormalizeFragmentId(qs(data.vname()));
		if (!anchorId.isEmpty()) {
			auto parsed = MakeBlock(BlockKind::Anchor);
			parsed.anchorId = anchorId;
			result->push_back(std::move(parsed));
		}
	}, [&](const MTPDpageBlockList &data) {
		auto parsed = MakeBlock(BlockKind::List);
		parsed.listKind = ListKind::Bullet;
		parsed.listItems.reserve(data.vitems().v.size());
		for (const auto &item : data.vitems().v) {
			auto listItem = ListItem();
			item.match([&](const MTPDpageListItemText &row) {
				listItem.taskState = TaskStateFromFlags(
					row.is_checkbox(),
					row.is_checked());
				listItem.text = ParseRichText(row.vtext(), context);
				AdoptAnchor(&listItem.anchorId, &listItem.text);
			}, [&](const MTPDpageListItemBlocks &row) {
				listItem.taskState = TaskStateFromFlags(
					row.is_checkbox(),
					row.is_checked());
				AppendBlocks(row.vblocks().v, &listItem.blocks, context);
				AdoptLeadingParagraphListItemText(&listItem);
			});
			parsed.listItems.push_back(std::move(listItem));
		}
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockBlockquote &data) {
		auto parsed = MakeBlock(BlockKind::Quote);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		parsed.caption = ParseRichText(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockBlockquoteBlocks &data) {
		auto parsed = MakeBlock(BlockKind::Quote);
		AppendBlocks(data.vblocks().v, &parsed.blocks, context);
		parsed.caption = ParseRichText(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockPullquote &data) {
		auto parsed = MakeBlock(BlockKind::Quote);
		parsed.pullquote = true;
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		parsed.caption = ParseRichText(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockPhoto &data) {
		const auto photoId = uint64(data.vphoto_id().v);
		const auto size = FindPhotoSize(*context, photoId);
		auto parsed = MakeBlock(BlockKind::Photo);
		if (context->source == ParseSource::InstantViewPage) {
			parsed.url = qs(data.vurl().value_or_empty());
		}
		parsed.width = size.width();
		parsed.height = size.height();
		parsed.photoId = photoId;
		parsed.spoiler = data.is_spoiler();
		parsed.photo = FindPhoto(*context, photoId);
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockVideo &data) {
		const auto documentId = uint64(data.vvideo_id().v);
		const auto info = FindDocumentInfo(*context, documentId);
		auto parsed = MakeBlock(BlockKind::Video);
		parsed.width = info.width;
		parsed.height = info.height;
		parsed.documentId = documentId;
		parsed.autoplay = data.is_autoplay();
		parsed.loop = data.is_loop();
		parsed.spoiler = data.is_spoiler();
		parsed.document = FindDocument(*context, documentId);
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockCover &data) {
		AppendBlock(data.vcover(), result, context);
	}, [&](const MTPDpageBlockEmbed &data) {
		auto parsed = MakeBlock(BlockKind::Embed);
		parsed.url = qs(data.vurl().value_or_empty());
		parsed.html = data.vhtml() ? qba(*data.vhtml()) : QByteArray();
		parsed.width = data.vw().value_or_empty();
		parsed.height = data.vh().value_or_empty();
		parsed.fullWidth = data.is_full_width() || (data.vw() && !data.vw()->v);
		parsed.fixedHeight = (data.vh() != nullptr);
		parsed.allowScrolling = data.is_allow_scrolling();
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockEmbedPost &data) {
		auto parsed = MakeBlock(BlockKind::EmbedPost);
		parsed.url = qs(data.vurl());
		parsed.author = qs(data.vauthor());
		parsed.date = data.vdate().v;
		parsed.photoId = uint64(data.vauthor_photo_id().v);
		parsed.photo = FindPhoto(*context, parsed.photoId);
		AppendBlocks(data.vblocks().v, &parsed.blocks, context);
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockCollage &data) {
		auto parsed = MakeBlock(BlockKind::GroupedMedia);
		parsed.mediaIntent = GroupedMediaIntent::Collage;
		parsed.mediaItems.reserve(data.vitems().v.size());
		for (const auto &item : data.vitems().v) {
			item.match([&](const MTPDpageBlockPhoto &row) {
				const auto photoId = uint64(row.vphoto_id().v);
				const auto size = FindPhotoSize(*context, photoId);
				parsed.mediaItems.push_back({
					.kind = BlockKind::Photo,
					.photo = FindPhoto(*context, photoId),
					.photoId = photoId,
					.width = size.width(),
					.height = size.height(),
					.spoiler = row.is_spoiler(),
				});
			}, [&](const MTPDpageBlockVideo &row) {
				const auto documentId = uint64(row.vvideo_id().v);
				const auto info = FindDocumentInfo(*context, documentId);
				parsed.mediaItems.push_back({
					.kind = BlockKind::Video,
					.document = FindDocument(*context, documentId),
					.documentId = documentId,
					.width = info.width,
					.height = info.height,
					.autoplay = row.is_autoplay(),
					.loop = row.is_loop(),
					.spoiler = row.is_spoiler(),
				});
			}, [](const auto &) {
			});
		}
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockSlideshow &data) {
		auto parsed = MakeBlock(BlockKind::GroupedMedia);
		parsed.mediaIntent = GroupedMediaIntent::Slideshow;
		parsed.mediaItems.reserve(data.vitems().v.size());
		for (const auto &item : data.vitems().v) {
			item.match([&](const MTPDpageBlockPhoto &row) {
				const auto photoId = uint64(row.vphoto_id().v);
				const auto size = FindPhotoSize(*context, photoId);
				parsed.mediaItems.push_back({
					.kind = BlockKind::Photo,
					.photo = FindPhoto(*context, photoId),
					.photoId = photoId,
					.width = size.width(),
					.height = size.height(),
					.spoiler = row.is_spoiler(),
				});
			}, [&](const MTPDpageBlockVideo &row) {
				const auto documentId = uint64(row.vvideo_id().v);
				const auto info = FindDocumentInfo(*context, documentId);
				parsed.mediaItems.push_back({
					.kind = BlockKind::Video,
					.document = FindDocument(*context, documentId),
					.documentId = documentId,
					.width = info.width,
					.height = info.height,
					.autoplay = row.is_autoplay(),
					.loop = row.is_loop(),
					.spoiler = row.is_spoiler(),
				});
			}, [](const auto &) {
			});
		}
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockChannel &data) {
		auto parsed = MakeBlock(BlockKind::Channel);
		parsed.peer = context->session->data().processChat(data.vchannel()).get();
		data.vchannel().match([&](const MTPDchannel &row) {
			parsed.channelId = row.vid().v;
			parsed.channelTitle = qs(row.vtitle());
			if (const auto username = row.vusername()) {
				parsed.username = qs(*username);
			}
		}, [&](const MTPDchannelForbidden &row) {
			parsed.channelId = row.vid().v;
			parsed.channelTitle = qs(row.vtitle());
		}, [&](const MTPDchat &row) {
			parsed.channelId = row.vid().v;
			parsed.channelTitle = qs(row.vtitle());
		}, [&](const MTPDchatForbidden &row) {
			parsed.channelId = row.vid().v;
			parsed.channelTitle = qs(row.vtitle());
		}, [](const auto &) {
		});
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockAudio &data) {
		const auto documentId = uint64(data.vaudio_id().v);
		const auto info = FindDocumentInfo(*context, documentId);
		auto parsed = MakeBlock(BlockKind::Audio);
		parsed.audioTitle = info.title;
		parsed.audioPerformer = info.performer;
		parsed.audioFileName = info.fileName;
		parsed.documentId = documentId;
		parsed.document = FindDocument(*context, documentId);
		parsed.audioDuration = info.duration;
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockKicker &data) {
		auto parsed = MakeHeadingBlock(5);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockHeading1 &data) {
		auto parsed = MakeHeadingBlock(1);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockHeading2 &data) {
		auto parsed = MakeHeadingBlock(2);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockHeading3 &data) {
		auto parsed = MakeHeadingBlock(3);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockHeading4 &data) {
		auto parsed = MakeHeadingBlock(4);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockHeading5 &data) {
		auto parsed = MakeHeadingBlock(5);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockHeading6 &data) {
		auto parsed = MakeHeadingBlock(6);
		parsed.text = ParseRichText(data.vtext(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockMath &data) {
		auto parsed = MakeBlock(BlockKind::Math);
		parsed.formula = qs(data.vsource());
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockTable &data) {
		auto parsed = MakeBlock(BlockKind::Table);
		parsed.bordered = data.is_bordered();
		parsed.striped = data.is_striped();
		parsed.text = ParseRichText(data.vtitle(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		parsed.tableRows.reserve(data.vrows().v.size());
		for (const auto &row : data.vrows().v) {
			row.match([&](const MTPDpageTableRow &rowData) {
				auto parsedRow = TableRow();
				parsedRow.cells.reserve(rowData.vcells().v.size());
				for (const auto &cell : rowData.vcells().v) {
					cell.match([&](const MTPDpageTableCell &cellData) {
						auto parsedCell = TableCell{
							.colspan = cellData.vcolspan().value_or(1),
							.rowspan = cellData.vrowspan().value_or(1),
							.header = cellData.is_header(),
							.alignment = TableCellAlignment(cellData),
							.verticalAlignment = TableCellVerticalAlignment(cellData),
						};
						if (const auto text = cellData.vtext()) {
							parsedCell.text = ParseRichText(*text, context);
							parsedCell.text.anchorId.clear();
							parsedCell.text.anchorIds.clear();
						}
						parsedRow.cells.push_back(std::move(parsedCell));
					});
				}
				parsed.tableRows.push_back(std::move(parsedRow));
			});
		}
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockOrderedList &data) {
		auto parsed = MakeBlock(BlockKind::List);
		parsed.listKind = ListKind::Ordered;
		parsed.orderedList.reversed = data.is_reversed();
		if (const auto start = data.vstart()) {
			parsed.orderedList.start = start->v;
		}
		if (const auto listType = data.vtype()) {
			parsed.orderedList.type = qs(*listType);
		}
		parsed.listItems.reserve(data.vitems().v.size());
		for (const auto &item : data.vitems().v) {
			auto listItem = ListItem();
			const auto fillOrderedData = [&](const auto &row) {
				if (const auto num = row.vnum()) {
					listItem.number.num = qs(*num);
				}
				if (const auto value = row.vvalue()) {
					listItem.number.value = value->v;
				}
				if (const auto itemType = row.vtype()) {
					listItem.number.type = qs(*itemType);
				}
			};
			item.match([&](const MTPDpageListOrderedItemText &row) {
				listItem.taskState = TaskStateFromFlags(
					row.is_checkbox(),
					row.is_checked());
				fillOrderedData(row);
				listItem.text = ParseRichText(row.vtext(), context);
				AdoptAnchor(&listItem.anchorId, &listItem.text);
			}, [&](const MTPDpageListOrderedItemBlocks &row) {
				listItem.taskState = TaskStateFromFlags(
					row.is_checkbox(),
					row.is_checked());
				fillOrderedData(row);
				AppendBlocks(row.vblocks().v, &listItem.blocks, context);
				AdoptLeadingParagraphListItemText(&listItem);
			});
			parsed.listItems.push_back(std::move(listItem));
		}
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockDetails &data) {
		auto parsed = MakeBlock(BlockKind::Details);
		parsed.open = data.is_open();
		parsed.text = ParseRichText(data.vtitle(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		AppendBlocks(data.vblocks().v, &parsed.blocks, context);
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockRelatedArticles &data) {
		auto parsed = MakeBlock(BlockKind::RelatedArticles);
		parsed.text = ParseRichText(data.vtitle(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.text);
		parsed.relatedArticles.reserve(data.varticles().v.size());
		for (const auto &article : data.varticles().v) {
			const auto &row = article.data();
			parsed.relatedArticles.push_back({
				.url = qs(row.vurl()),
				.webpageId = uint64(row.vwebpage_id().v),
				.photo = FindPhoto(
					*context,
					uint64(row.vphoto_id().value_or_empty())),
				.photoId = uint64(row.vphoto_id().value_or_empty()),
				.title = qs(row.vtitle().value_or_empty()).trimmed(),
				.description = qs(row.vdescription().value_or_empty()).trimmed(),
				.author = qs(row.vauthor().value_or_empty()).trimmed(),
				.publishedDate = row.vpublished_date().value_or_empty(),
			});
		}
		result->push_back(std::move(parsed));
	}, [&](const MTPDpageBlockMap &data) {
		auto parsed = MakeBlock(BlockKind::Map);
		parsed.width = NonZeroMapWidth(data.vw().v);
		parsed.height = NonZeroMapHeight(data.vh().v);
		parsed.zoom = data.vzoom().v;
		data.vgeo().match([&](const MTPDgeoPoint &row) {
			parsed.latitude = row.vlat().v;
			parsed.longitude = row.vlong().v;
			parsed.accessHash = row.vaccess_hash().v;
		}, [](const auto &) {
		});
		parsed.caption = ParseCaption(data.vcaption(), context);
		AdoptAnchor(&parsed.anchorId, &parsed.caption);
		result->push_back(std::move(parsed));
	}, [&](const MTPDinputPageBlockMap &) {
		result->push_back(MakeBlock(BlockKind::Unsupported));
	});
}

void AppendBlocks(
		const QVector<MTPPageBlock> &blocks,
		std::vector<Block> *result,
		ParseContext *context) {
	result->reserve(result->size() + blocks.size());
	for (const auto &block : blocks) {
		AppendBlock(block, result, context);
	}
}

void ExpandInlineTextObjects(TextWithEntities *text, bool withIcons) {
	auto &entities = text->entities;
	for (auto i = entities.begin(); i != entities.end();) {
		if (i->type() != EntityType::CustomEmoji) {
			++i;
			continue;
		}
		const auto object = Markdown::ParseInlineTextObjectEntity(
			i->data());
		if (!object) {
			++i;
			continue;
		}
		const auto replacement = v::match(object->data, [](
				const Markdown::InlineTextObjectFormulaData &data) {
			return data.trimmedTex;
		}, [](const Markdown::InlineTextObjectIvImageData &data) {
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
			== Markdown::InlineTextObjectKind::Formula);
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

void AppendSummaryLine(
		TextWithEntities *result,
		TextWithEntities &&line,
		bool withIcons) {
	ExpandInlineTextObjects(&line, withIcons);
	TextUtilities::Trim(line);
	if (line.empty()) {
		return;
	}
	if (!result->empty()) {
		result->append(QChar('\n'));
	}
	result->append(std::move(line));
}

void AppendSummaryLine(
		TextWithEntities *result,
		const TextWithEntities &line,
		bool withIcons,
		const QString &prefix = QString()) {
	auto prepared = TextWithEntities();
	if (!prefix.isEmpty()) {
		prepared.append(prefix);
	}
	prepared.append(line);
	AppendSummaryLine(result, std::move(prepared), withIcons);
}

void AppendSummaryLine(
		TextWithEntities *result,
		const RichText &line,
		bool withIcons,
		const QString &prefix = QString()) {
	AppendSummaryLine(result, line.text, withIcons, prefix);
}

[[nodiscard]] QString MediaSummaryFallback(const Block &block) {
	if (block.kind == BlockKind::Photo) {
		return tr::lng_in_dlg_photo(tr::now);
	} else if (block.kind == BlockKind::Video) {
		return tr::lng_in_dlg_video(tr::now);
	} else if (block.kind == BlockKind::Audio) {
		return tr::lng_in_dlg_audio_file(tr::now);
	} else if (block.kind == BlockKind::Map) {
		return tr::lng_maps_point(tr::now);
	} else if (block.kind == BlockKind::GroupedMedia) {
		auto photos = false;
		auto videos = false;
		for (const auto &item : block.mediaItems) {
			if (item.kind == BlockKind::Video) {
				videos = true;
			} else {
				photos = true;
			}
		}
		if (videos && !photos) {
			return tr::lng_in_dlg_video(tr::now);
		} else if (photos && !videos) {
			return tr::lng_in_dlg_photo(tr::now);
		}
		return tr::lng_in_dlg_album(tr::now);
	}
	return QString();
}

[[nodiscard]] QString FooterText(const RelatedArticle &article) {
	if (article.publishedDate && !article.author.isEmpty()) {
		return article.author + u", "_q + DateText(article.publishedDate);
	} else if (article.publishedDate) {
		return DateText(article.publishedDate);
	}
	return article.author;
}

void AppendSummaryBlock(
	TextWithEntities *result,
	const Block &block,
	bool withIcons);

void AppendSummaryBlocks(
		TextWithEntities *result,
		const std::vector<Block> &blocks,
		bool withIcons) {
	for (const auto &block : blocks) {
		AppendSummaryBlock(result, block, withIcons);
	}
}

[[nodiscard]] TextWithEntities FlattenSummaryBlocks(
		const std::vector<Block> &blocks,
		bool withIcons) {
	auto result = TextWithEntities();
	AppendSummaryBlocks(&result, blocks, withIcons);
	TextUtilities::Trim(result);
	return result;
}

[[nodiscard]] bool SimpleTextEntitiesAllowed(const TextWithEntities &text) {
	for (const auto &entity : text.entities) {
		const auto type = entity.type();
		if (type == EntityType::Subscript
			|| type == EntityType::Superscript
			|| type == EntityType::Marked) {
			return false;
		} else if (type == EntityType::CustomEmoji
			&& Markdown::ParseInlineTextObjectEntity(entity.data())) {
			// Math formulas and inline images are stored as CustomEmoji
			// entities over an object replacement character; a normal
			// message can't carry them, only real custom emoji entities.
			return false;
		}
	}
	return true;
}

// Drops the inline entities that a normal (non-premium) message can't carry.
// Math formulas are expanded to plain text by ExpandInlineTextObjects (in
// AppendSummaryLine, which always runs without icons on the simple-text
// paths), so only Subscript/Superscript/Marked remain to strip; real custom
// emoji are kept (they're allowed in normal messages).
void RemovePremiumOnlyInlineEntities(TextWithEntities *text) {
	auto &list = text->entities;
	for (auto i = list.begin(); i != list.end();) {
		const auto type = i->type();
		if (type == EntityType::Subscript
			|| type == EntityType::Superscript
			|| type == EntityType::Marked) {
			i = list.erase(i);
		} else {
			++i;
		}
	}
}

void AppendSimpleBlock(
		TextWithEntities *result,
		TextWithEntities &&block,
		EntityType wrap = EntityType::Invalid,
		const QString &wrapData = QString()) {
	TextUtilities::Trim(block);
	if (block.empty()) {
		return;
	}
	if (wrap != EntityType::Invalid) {
		block.entities.push_back(
			EntityInText(wrap, 0, int(block.text.size()), wrapData));
	}
	if (!result->empty()) {
		result->append(QChar('\n'));
	}
	result->append(std::move(block));
}

// Computes the length text.text would have after TextUtilities::Trim(),
// without copying or mutating anything. Must mirror Trim() exactly: right
// trim by IsTrimmed(), then left trim stopping at the first monospace
// entity offset (leading whitespace inside inline code is preserved).
[[nodiscard]] int TrimmedLength(const TextWithEntities &text) {
	const auto begin = text.text.constData();
	auto end = begin + text.text.size();
	while (end != begin && Ui::Text::IsTrimmed(*(end - 1))) {
		--end;
	}
	if (end == begin) {
		return 0;
	}
	const auto firstMonospaceOffset = EntityInText::FirstMonospaceOffset(
		text.entities,
		int(end - begin));
	auto start = begin;
	while ((start - begin) != firstMonospaceOffset
		&& Ui::Text::IsTrimmed(*start)) {
		++start;
	}
	return int(end - start);
}

// Sinks for SerializeAsSimpleTo(): SimpleTextBuilder builds the actual
// serialized text, SimpleTextCounter only computes the length that text
// would have, skipping the string building entirely, so that the check
// is cheap enough to run on every content change.
struct SimpleTextBuilder {
	TextWithEntities result;

	void append(
			const TextWithEntities &text,
			EntityType wrap = EntityType::Invalid,
			const QString &wrapData = QString()) {
		AppendSimpleBlock(&result, TextWithEntities(text), wrap, wrapData);
	}
	void appendQuote(SimpleTextBuilder &&body) {
		AppendSimpleBlock(
			&result,
			std::move(body.result),
			EntityType::Blockquote);
	}
	[[nodiscard]] int length() const {
		return int(result.text.size());
	}
};

struct SimpleTextCounter {
	int result = 0;

	void append(
			const TextWithEntities &text,
			EntityType = EntityType::Invalid,
			const QString & = QString()) {
		appendLength(TrimmedLength(text));
	}
	void appendQuote(SimpleTextCounter &&body) {
		appendLength(body.result);
	}
	void appendLength(int length) {
		if (length > 0) {
			// The 1 is for the '\n' AppendSimpleBlock() would insert.
			result += (result > 0 ? 1 : 0) + length;
		}
	}
	[[nodiscard]] int length() const {
		return result;
	}
};

template <typename Accumulator>
[[nodiscard]] bool CollectSimpleQuote(
		const Block &quote,
		Accumulator &body) {
	if (quote.pullquote
		|| !quote.author.trimmed().isEmpty()
		|| !quote.caption.text.text.trimmed().isEmpty()) {
		return false;
	}
	if (!quote.text.text.empty()) {
		if (!SimpleTextEntitiesAllowed(quote.text.text)) {
			return false;
		}
		body.append(quote.text.text);
	}
	for (const auto &child : quote.blocks) {
		if (child.kind != BlockKind::Paragraph
			|| !SimpleTextEntitiesAllowed(child.text.text)) {
			return false;
		}
		body.append(child.text.text);
	}
	return true;
}

// Single source of truth for what can be sent as a simple message, shared
// by SerializeAsSimple() and CanSerializeAsSimple(), so that the two can't
// drift apart: returns false when the page can't be sent as one (blocks or
// entities a normal message can't carry, empty or over-limit text).
template <typename Accumulator>
[[nodiscard]] bool SerializeAsSimpleTo(
		const RichPage &page,
		not_null<Main::Session*> session,
		Accumulator &to) {
	for (const auto &block : page.blocks) {
		switch (block.kind) {
		case BlockKind::Paragraph:
			if (!SimpleTextEntitiesAllowed(block.text.text)) {
				return false;
			}
			to.append(block.text.text);
			break;
		case BlockKind::Code:
			if (!block.text.text.entities.isEmpty()) {
				return false;
			}
			to.append(block.text.text, EntityType::Pre, block.language);
			break;
		case BlockKind::Quote: {
			auto body = Accumulator();
			if (!CollectSimpleQuote(block, body)) {
				return false;
			}
			to.appendQuote(std::move(body));
			break;
		}
		default:
			return false;
		}
	}
	if (!to.length()) {
		return false;
	}
	const auto lengthLimit = ::Data::PremiumLimits(session)
		.messageLengthCurrent();
	return to.length() <= lengthLimit;
}

[[nodiscard]] bool RichBlockIsPlain(const Block &block) {
	const auto hasEntities = [](const TextWithEntities &text) {
		return !text.entities.isEmpty();
	};
	if (block.kind == BlockKind::Paragraph) {
		return !hasEntities(block.text.text);
	} else if (block.kind == BlockKind::Quote) {
		if (block.pullquote
			|| !block.author.trimmed().isEmpty()
			|| !block.caption.text.text.trimmed().isEmpty()
			|| hasEntities(block.text.text)) {
			return false;
		}
		for (const auto &child : block.blocks) {
			if (child.kind != BlockKind::Paragraph
				|| hasEntities(child.text.text)) {
				return false;
			}
		}
		return true;
	}
	return false;
}

void AppendSummaryBlock(
		TextWithEntities *result,
		const Block &block,
		bool withIcons) {
	switch (block.kind) {
	case BlockKind::Unsupported:
	case BlockKind::Divider:
	case BlockKind::Anchor:
		return;
	case BlockKind::Heading:
	case BlockKind::Paragraph:
	case BlockKind::Footer:
	case BlockKind::Code:
	case BlockKind::Thinking:
		AppendSummaryLine(result, block.text, withIcons);
		return;
	case BlockKind::AuthorDate: {
		auto line = block.text.text;
		if (block.date) {
			if (!line.empty()) {
				line.append(u" - "_q);
			}
			line.append(DateText(block.date));
		}
		AppendSummaryLine(result, std::move(line), withIcons);
		return;
	}
	case BlockKind::List: {
		auto ordered = OrderedListSequenceStart(block);
		const auto step = block.orderedList.reversed ? -1 : 1;
		for (const auto &item : block.listItems) {
			auto prefix = QString();
			const auto orderedValue = item.number.value.value_or(ordered);
			if (item.taskState == TaskState::Unchecked) {
				prefix = u"[ ] "_q;
			} else if (item.taskState == TaskState::Checked) {
				prefix = u"[x] "_q;
			} else if (block.listKind == ListKind::Ordered) {
				const auto marker = OrderedMarkerText(
					block.orderedList,
					item.number,
					ordered);
				prefix = marker.isEmpty() ? QString() : (marker + u" "_q);
			} else {
				prefix = u"- "_q;
			}
			if (!item.text.text.empty()) {
				AppendSummaryLine(result, item.text, withIcons, prefix);
			} else {
				auto nested = FlattenSummaryBlocks(item.blocks, withIcons);
				AppendSummaryLine(result, std::move(nested), withIcons, prefix);
			}
			if (block.listKind == ListKind::Ordered) {
				ordered = orderedValue + step;
			}
		}
		return;
	}
	case BlockKind::Quote: {
		if (!withIcons) {
			AppendSummaryLine(result, block.text, withIcons);
			AppendSummaryBlocks(result, block.blocks, withIcons);
			AppendSummaryLine(result, block.caption, withIcons);
			return;
		}
		auto inner = tr::marked();
		AppendSummaryLine(&inner, block.text, withIcons);
		AppendSummaryBlocks(&inner, block.blocks, withIcons);
		AppendSummaryLine(&inner, block.caption, withIcons);
		if (inner.empty()) {
			return;
		}
		auto line = Ui::Text::IconEmoji(
			(block.pullquote
				? &st::ivSummaryPullquoteIcon
				: &st::ivSummaryBlockquoteIcon),
			(block.pullquote
				? tr::lng_article_insert_pullquote(tr::now)
				: tr::lng_menu_formatting_blockquote(tr::now)) + QChar(' '));
		line.append(std::move(inner));
		AppendSummaryLine(result, std::move(line), withIcons);
		return;
	}
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::GroupedMedia:
	case BlockKind::Map: {
		if (!withIcons) {
			if (!block.caption.text.empty()) {
				AppendSummaryLine(result, block.caption, withIcons);
			} else {
				AppendSummaryLine(
					result,
					TextWithEntities::Simple(MediaSummaryFallback(block)),
					withIcons);
			}
			return;
		}
		const auto icon = (block.kind == BlockKind::Audio)
			? &st::ivSummaryAudioIcon
			: (block.kind == BlockKind::Map)
			? &st::ivSummaryLocationIcon
			: &st::ivSummaryMediaIcon;
		auto line = Ui::Text::IconEmoji(
			icon,
			MediaSummaryFallback(block) + QChar(' '));
		line.append(block.caption.text);
		AppendSummaryLine(result, std::move(line), withIcons);
		return;
	}
	case BlockKind::Embed:
		if (!block.caption.text.empty()) {
			AppendSummaryLine(result, block.caption, withIcons);
		} else if (!block.url.isEmpty()) {
			AppendSummaryLine(
				result,
				TextWithEntities::Simple(block.url),
				withIcons);
		}
		return;
	case BlockKind::EmbedPost: {
		auto line = QString();
		if (!block.author.isEmpty()) {
			line = block.author;
		}
		if (block.date) {
			if (!line.isEmpty()) {
				line += u", "_q;
			}
			line += DateText(block.date);
		}
		if (!line.isEmpty()) {
			AppendSummaryLine(
				result,
				TextWithEntities::Simple(line),
				withIcons);
		}
		AppendSummaryBlocks(result, block.blocks, withIcons);
		AppendSummaryLine(result, block.caption, withIcons);
		return;
	}
	case BlockKind::Channel:
		if (block.peer) {
			AppendSummaryLine(
				result,
				TextWithEntities::Simple(block.peer->name()),
				withIcons);
		}
		return;
	case BlockKind::Math:
		if (withIcons && !block.formula.isEmpty()) {
			AppendSummaryLine(result, Ui::Text::IconEmoji(
				&st::ivSummaryMathIcon,
				block.formula), withIcons);
		} else {
			AppendSummaryLine(
				result,
				TextWithEntities::Simple(block.formula),
				withIcons);
		}
		return;
	case BlockKind::Table:
		if (withIcons) {
			AppendSummaryLine(result, Ui::Text::IconEmoji(
				&st::ivSummaryTableIcon,
				tr::lng_in_dlg_table(tr::now)), withIcons);
		} else if (!block.text.text.empty()) {
			AppendSummaryLine(result, block.text, withIcons);
		} else {
			AppendSummaryLine(
				result,
				TextWithEntities::Simple(tr::lng_in_dlg_table(tr::now)),
				withIcons);
		}
		return;
	case BlockKind::Details:
		AppendSummaryLine(result, block.text, withIcons);
		AppendSummaryBlocks(result, block.blocks, withIcons);
		return;
	case BlockKind::RelatedArticles:
		AppendSummaryLine(result, block.text, withIcons);
		for (const auto &article : block.relatedArticles) {
			if (!article.title.isEmpty()) {
				AppendSummaryLine(
					result,
					TextWithEntities::Simple(article.title),
					withIcons);
			}
			if (!article.description.isEmpty()) {
				AppendSummaryLine(
					result,
					TextWithEntities::Simple(article.description),
					withIcons);
			}
			const auto footer = FooterText(article);
			if (!footer.isEmpty()) {
				AppendSummaryLine(
					result,
					TextWithEntities::Simple(footer),
					withIcons);
			}
		}
		return;
	}
}

std::shared_ptr<const RichPage> ParsePage(
		not_null<Main::Session*> session,
		const MTPPage &page,
		const MTPDwebPage *webpage) {
	return page.match([&](const MTPDpage &data) {
		auto result = std::make_shared<RichPage>();
		auto context = ParseContext(session, ParseSource::InstantViewPage);
		result->url = qs(data.vurl());
		result->rtl = data.is_rtl();
		result->part = data.is_part();
		result->views = data.vviews().value_or_empty();
		for (const auto &photo : data.vphotos().v) {
			RememberPhoto(&context, photo);
		}
		for (const auto &document : data.vdocuments().v) {
			RememberDocument(&context, document);
		}
		if (webpage) {
			RememberWebPageMedia(&context, *webpage);
		}
		AppendBlocks(data.vblocks().v, &result->blocks, &context);
		return std::shared_ptr<const RichPage>(std::move(result));
	}, [](const auto &) {
		return std::shared_ptr<const RichPage>();
	});
}

[[nodiscard]] bool RichBlockIsFlattenSafe(const RichPage::Block &block) {
	switch (block.kind) {
	case BlockKind::Table:
	case BlockKind::Math:
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::GroupedMedia:
	case BlockKind::Map:
		return false;
	default:
		break;
	}
	for (const auto &item : block.mediaItems) {
		switch (item.kind) {
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
			return false;
		default:
			break;
		}
	}
	for (const auto &child : block.blocks) {
		if (!RichBlockIsFlattenSafe(child)) {
			return false;
		}
	}
	for (const auto &listItem : block.listItems) {
		for (const auto &child : listItem.blocks) {
			if (!RichBlockIsFlattenSafe(child)) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] std::optional<bool> RichTextRtl(const RichText &text) {
	const auto &plain = text.text.text;
	if (plain.trimmed().isEmpty()) {
		return std::nullopt;
	}
	return plain.isRightToLeft();
}

[[nodiscard]] std::optional<bool> BlocksTextRtl(
		const std::vector<Block> &blocks) {
	for (const auto &block : blocks) {
		if (const auto result = RichTextRtl(block.text)) {
			return result;
		}
		if (const auto result = BlocksTextRtl(block.blocks)) {
			return result;
		}
		for (const auto &item : block.listItems) {
			if (const auto result = RichTextRtl(item.text)) {
				return result;
			}
			if (const auto result = BlocksTextRtl(item.blocks)) {
				return result;
			}
		}
	}
	return std::nullopt;
}

} // namespace

bool RichPagesEqual(
		const RichPage &a,
		const RichPage &b) {
	return (a == b);
}

RichMessageLimits ResolveRichMessageLimits(not_null<Main::Session*> session) {
	const auto &config = session->appConfig();
	auto result = RichMessageLimits();
	result.lengthLimit = config.get<int>(
		u"rich_message_length_limit"_q,
		result.lengthLimit);
	result.maxBlocks = config.get<int>(
		u"rich_message_max_blocks"_q,
		result.maxBlocks);
	result.maxDepth = config.get<int>(
		u"rich_message_max_depth"_q,
		result.maxDepth);
	result.maxMedia = config.get<int>(
		u"rich_message_max_media"_q,
		result.maxMedia);
	result.maxTableCols = config.get<int>(
		u"rich_message_max_table_cols"_q,
		result.maxTableCols);
	return result;
}

std::optional<RichMessageLimitError> ValidateRichMessage(
		const RichPage &page,
		const RichMessageLimits &limits) {
	const auto metrics = ComputeRichMessageMetrics(page, limits);
	if (metrics.textLength > limits.lengthLimit) {
		return RichMessageLimitError::Length;
	} else if (metrics.blockCount > limits.maxBlocks) {
		return RichMessageLimitError::Blocks;
	} else if (metrics.maxDepth > limits.maxDepth) {
		return RichMessageLimitError::Depth;
	} else if (metrics.mediaCount > limits.maxMedia) {
		return RichMessageLimitError::Media;
	} else if (metrics.maxTableColumns > limits.maxTableCols) {
		return RichMessageLimitError::TableColumns;
	}
	return std::nullopt;
}

QString EncodeRichPageLinkUrl(
		const QString &url,
		uint64 webpageId) {
	return u"internal:wrapped?url="_q
		+ qthelp::url_encode(url)
		+ u"&context=iv&webpage_id="_q
		+ QString::number(webpageId);
}

std::optional<RichPageLinkUrl> DecodeRichPageLinkUrl(const QString &data) {
	const auto wrappedPrefix = u"internal:wrapped?"_q;
	if (!data.startsWith(wrappedPrefix)) {
		return std::nullopt;
	}
	const auto params = qthelp::url_parse_params(data.mid(wrappedPrefix.size()));
	if (params.value(u"context"_q) != u"iv"_q) {
		return std::nullopt;
	}
	const auto url = params.value(u"url"_q);
	if (url.isEmpty()) {
		return std::nullopt;
	}
	const auto webpageIdValue = params.value(u"webpage_id"_q);
	auto webpageId = uint64();
	if (!webpageIdValue.isEmpty()) {
		auto ok = false;
		webpageId = webpageIdValue.toULongLong(&ok);
		if (!ok) {
			return std::nullopt;
		}
	}
	return RichPageLinkUrl{
		.url = url,
		.webpageId = webpageId,
	};
}

std::shared_ptr<const RichPage> ParseRichPage(
		not_null<Main::Session*> session,
		const MTPRichMessage &message,
		RichParseMode mode) {
	auto result = std::make_shared<RichPage>();
	auto context = ParseContext(session, ParseSource::RichMessage);
	context.displayTextDiff = (mode == RichParseMode::DisplayTextDiff);
	const auto &data = message.data();
	result->rtl = data.is_rtl();
	result->part = data.is_part();
	for (const auto &photo : data.vphotos().v) {
		RememberPhoto(&context, photo);
	}
	for (const auto &document : data.vdocuments().v) {
		RememberDocument(&context, document);
	}
	AppendBlocks(data.vblocks().v, &result->blocks, &context);
	return result;
}

std::shared_ptr<const RichPage> ParseRichPage(
		not_null<Main::Session*> session,
		const MTPPage &page) {
	return ParsePage(session, page, nullptr);
}

std::shared_ptr<const RichPage> ParseRichPage(
		not_null<Main::Session*> session,
		const MTPDwebPage &webpage) {
	const auto cachedPage = webpage.vcached_page();
	if (!cachedPage) {
		return std::shared_ptr<const RichPage>();
	}
	return ParsePage(session, *cachedPage, &webpage);
}

TextWithEntities FlattenRichPageSummary(
		const RichPage &page,
		bool emptyFallback) {
	auto result = FlattenSummaryBlocks(page.blocks, true);
	TextUtilities::Trim(result);
	if (result.empty() && emptyFallback) {
		result = TextWithEntities::Simple(tr::lng_message_empty(tr::now));
	}
	return result;
}

TextWithEntities FlattenRichPageSummary(
		const std::shared_ptr<const RichPage> &page,
		bool emptyFallback) {
	return page
		? FlattenRichPageSummary(*page, emptyFallback)
		: TextWithEntities();
}

TextWithEntities FlattenRichPageToSimpleText(const RichPage &page) {
	auto result = TextWithEntities();
	for (const auto &block : page.blocks) {
		switch (block.kind) {
		case BlockKind::Code: {
			// Code blocks are allowed at the top level as a Pre entity, but
			// their content is sent as plain text without any inline entities.
			auto inner = block.text.text;
			ExpandInlineTextObjects(&inner, false);
			inner.entities.clear();
			AppendSimpleBlock(
				&result,
				std::move(inner),
				EntityType::Pre,
				block.language);
			break;
		}
		case BlockKind::Quote: {
			// Blockquotes are allowed at the top level as a Blockquote entity;
			// the author is dropped and the inner content is flattened into a
			// single TextWithEntities (keeping allowed inline formatting, no
			// nested block formatting).
			auto inner = TextWithEntities();
			AppendSummaryLine(&inner, block.text, false);
			AppendSummaryBlocks(&inner, block.blocks, false);
			AppendSummaryLine(&inner, block.caption, false);
			RemovePremiumOnlyInlineEntities(&inner);
			AppendSimpleBlock(
				&result,
				std::move(inner),
				EntityType::Blockquote);
			break;
		}
		default: {
			// Every other block (heading, list, table, math, paragraph, ...)
			// is flattened to plain text lines, keeping the inline formatting a
			// normal message can carry.
			auto piece = TextWithEntities();
			AppendSummaryBlock(&piece, block, false);
			RemovePremiumOnlyInlineEntities(&piece);
			AppendSummaryLine(&result, std::move(piece), false);
			break;
		}
		}
	}
	TextUtilities::Trim(result);
	if (result.empty()) {
		result = TextWithEntities::Simple(tr::lng_message_empty(tr::now));
	}
	return result;
}

bool DetermineRichPageRtl(const RichPage &page) {
	return BlocksTextRtl(page.blocks).value_or(false);
}

std::optional<TextWithEntities> SerializeAsSimple(
		const RichPage &page,
		not_null<Main::Session*> session) {
	auto to = SimpleTextBuilder();
	if (!SerializeAsSimpleTo(page, session, to)) {
		return std::nullopt;
	}
	TextUtilities::Trim(to.result);
	return std::move(to.result);
}

bool CanSerializeAsSimple(
		const RichPage &page,
		not_null<Main::Session*> session) {
	auto to = SimpleTextCounter();
	return SerializeAsSimpleTo(page, session, to);
}

bool RichPageUsesPremiumFormatting(const RichPage &page) {
	for (const auto &block : page.blocks) {
		if (!RichBlockIsPlain(block)) {
			return true;
		}
	}
	return false;
}

bool RichPageIsFlattenSafe(const RichPage &page) {
	for (const auto &block : page.blocks) {
		if (!RichBlockIsFlattenSafe(block)) {
			return false;
		}
	}
	return true;
}

RichPage SplitTextIntoRichPage(TextWithEntities text) {
	auto page = RichPage();

	const auto isBlockEntity = [](const EntityInText &entity) {
		const auto type = entity.type();
		return (type == EntityType::Pre)
			|| (type == EntityType::Blockquote);
	};
	const auto stripBlockEntities = [&](TextWithEntities &part) {
		part.entities.erase(
			ranges::remove_if(part.entities, isBlockEntity),
			part.entities.end());
	};
	const auto emitParagraph = [&](int from, int to) {
		auto paragraph = Ui::Text::Mid(text, from, to - from);
		stripBlockEntities(paragraph);
		TextUtilities::Trim(paragraph);
		if (!paragraph.empty()) {
			page.blocks.push_back(Block{
				.kind = BlockKind::Paragraph,
				.text = { std::move(paragraph) },
			});
		}
	};

	struct Segment {
		int offset = 0;
		int length = 0;
		EntityType type = EntityType::Invalid;
		QString data;
	};
	auto segments = std::vector<Segment>();
	for (const auto &entity : text.entities) {
		if (isBlockEntity(entity)) {
			segments.push_back({
				entity.offset(),
				entity.length(),
				entity.type(),
				entity.data(),
			});
		}
	}
	ranges::sort(segments, ranges::less(), &Segment::offset);

	auto cursor = 0;
	const auto size = int(text.text.size());
	for (const auto &segment : segments) {
		if (segment.offset < cursor) {
			continue;
		}
		emitParagraph(cursor, segment.offset);
		auto body = Ui::Text::Mid(text, segment.offset, segment.length);
		stripBlockEntities(body);
		TextUtilities::Trim(body);
		cursor = segment.offset + segment.length;
		if (body.empty()) {
			continue;
		}
		if (segment.type == EntityType::Pre) {
			page.blocks.push_back(Block{
				.kind = BlockKind::Code,
				.text = { std::move(body) },
				.language = segment.data,
			});
		} else {
			page.blocks.push_back(Block{
				.kind = BlockKind::Quote,
				.text = { std::move(body) },
			});
		}
	}
	emitParagraph(cursor, size);

	return page;
}

RichPage SplitTextIntoRichPage(const TextWithTags &text) {
	return SplitTextIntoRichPage({
		text.text,
		TextUtilities::ConvertTextTagsToEntities(text.tags),
	});
}

} // namespace Iv

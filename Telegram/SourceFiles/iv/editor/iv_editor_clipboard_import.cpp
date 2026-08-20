/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_clipboard_import.h"

#include "core/mime_type.h"
#include "iv/editor/iv_editor_page_blocks.h"
#include "iv/editor/iv_editor_session.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "platform/platform_file_utilities.h"
#include "storage/storage_media_prepare.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_html_tags.h"
#include "ui/text/text_utilities.h"

#include "styles/style_boxes.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QUrlQuery>
#include <QtGui/QImage>

#include <array>

namespace Iv::Editor {
namespace {

constexpr auto kMaxCellLength = 4096;
constexpr auto kMaxDataUriLength = 24 * 1024 * 1024;
constexpr auto kMaxDataUriHeaderLength = 256;

[[nodiscard]] QString QtWindowsMimeName(const QString &name) {
	return u"application/x-qt-windows-mime;value=\"%1\""_q.arg(name);
}

[[nodiscard]] bool HasNativeFormat(
		not_null<const QMimeData*> data,
		const QString &name) {
	return data->hasFormat(name)
		|| data->hasFormat(QtWindowsMimeName(name));
}

[[nodiscard]] bool HasSpreadsheetHint(not_null<const QMimeData*> data) {
	static const auto kNames = std::array{
		u"Biff12"_q,
		u"Biff8"_q,
		u"Biff5"_q,
		u"XML Spreadsheet"_q,
	};
	for (const auto &name : kNames) {
		if (HasNativeFormat(data, name)) {
			return true;
		}
	}
	static const auto kPrefixes = std::array{
		u"application/x-libreoffice-internal-id-"_q,
		u"application/x-openoffice-embed-source-xml"_q,
	};
	for (const auto &format : data->formats()) {
		for (const auto &prefix : kPrefixes) {
			if (format.startsWith(prefix)) {
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] bool RangeTextIsEmpty(QStringView html, int from, int till) {
	if (from >= till) {
		return true;
	}
	const auto parsed = TextUtilities::TextWithTagsFromHtmlFragment(
		html.mid(from, till - from));
	return parsed.text.trimmed().isEmpty();
}

[[nodiscard]] bool TableCoversWholeHtml(
		QStringView html,
		const TextUtilities::HtmlTable &table) {
	return RangeTextIsEmpty(html, 0, table.sourceFrom)
		&& RangeTextIsEmpty(html, table.sourceTill, int(html.size()));
}

[[nodiscard]] RichPage::TableAlignment ConvertAlignment(
		TextUtilities::HtmlTableAlignment alignment) {
	using Source = TextUtilities::HtmlTableAlignment;
	switch (alignment) {
	case Source::Center: return RichPage::TableAlignment::Center;
	case Source::Right: return RichPage::TableAlignment::Right;
	case Source::Left:
	case Source::Default: break;
	}
	return RichPage::TableAlignment::Left;
}

[[nodiscard]] TextWithEntities ConvertImportedText(TextWithTags text) {
	return ConvertEditorTagsToRichText(std::move(text));
}

[[nodiscard]] TextWithEntities WithoutBlockOnlyEntities(
		TextWithEntities text) {
	DegradeBlockOnlyEntities(text);
	return text;
}

[[nodiscard]] TextWithEntities ConvertImportedCellText(
		const TextWithTags &text) {
	return WithoutBlockOnlyEntities({
		text.text,
		TextUtilities::ConvertTextTagsToEntities(text.tags),
	});
}

[[nodiscard]] RichPage::Block ConvertTable(
		const TextUtilities::HtmlTable &table) {
	auto result = RichPage::Block();
	result.kind = RichPage::BlockKind::Table;
	result.bordered = true;
	result.text.text = WithoutBlockOnlyEntities(
		ConvertImportedText(table.caption));
	result.tableRows.reserve(table.rows.size());
	for (const auto &row : table.rows) {
		auto converted = RichPage::TableRow();
		converted.cells.reserve(row.cells.size());
		for (const auto &cell : row.cells) {
			converted.cells.push_back({
				.text = {
					.text = ConvertImportedCellText(cell.text),
				},
				.colspan = cell.colspan,
				.rowspan = cell.rowspan,
				.header = cell.header,
				.alignment = ConvertAlignment(cell.alignment),
			});
		}
		result.tableRows.push_back(std::move(converted));
	}
	return result;
}

[[nodiscard]] RichPage::TaskState ConvertTaskState(
		TextUtilities::HtmlTaskState state) {
	using Source = TextUtilities::HtmlTaskState;
	switch (state) {
	case Source::Unchecked: return RichPage::TaskState::Unchecked;
	case Source::Checked: return RichPage::TaskState::Checked;
	case Source::None: break;
	}
	return RichPage::TaskState::None;
}

[[nodiscard]] uint64 MediaIdFromIdentity(
		const QString &identity,
		const QString &kind) {
	const auto prefix = u"tg://"_q + kind + u"?"_q;
	if (!identity.startsWith(prefix, Qt::CaseInsensitive)) {
		return 0;
	}
	const auto query = QUrlQuery(identity.mid(prefix.size()));
	const auto value = query.queryItemValue(u"id"_q);
	auto ok = false;
	const auto result = value.toULongLong(&ok);
	return ok ? result : 0;
}

[[nodiscard]] bool LooksLikeMediaExtension(const QString &suffix) {
	static const auto known = std::array{
		u"jpg"_q, u"jpeg"_q, u"png"_q, u"gif"_q, u"webp"_q, u"bmp"_q,
		u"heic"_q, u"heif"_q, u"tiff"_q, u"avif"_q,
		u"mp4"_q, u"mov"_q, u"m4v"_q, u"webm"_q, u"mkv"_q, u"avi"_q,
		u"mp3"_q, u"m4a"_q, u"ogg"_q, u"oga"_q, u"opus"_q, u"flac"_q,
		u"wav"_q, u"aac"_q,
	};
	const auto lower = suffix.toLower();
	for (const auto &entry : known) {
		if (lower == entry) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QByteArray DataUriImageContent(const QString &source) {
	const auto comma = source.indexOf(QChar(','));
	if (comma < 0
		|| comma > kMaxDataUriHeaderLength
		|| source.size() > kMaxDataUriLength
		|| !source.startsWith(u"data:image/"_q, Qt::CaseInsensitive)) {
		return QByteArray();
	}
	auto header = source.mid(5, comma - 5);
	header.remove(QChar(' '));
	if (!header.endsWith(u";base64"_q, Qt::CaseInsensitive)) {
		return QByteArray();
	}
	const auto decoded = QByteArray::fromBase64(
		QStringView(source).mid(comma + 1).toLatin1());
	return (decoded.isEmpty() || QImage::fromData(decoded).isNull())
		? QByteArray()
		: decoded;
}

[[nodiscard]] QString LocalMediaPath(
		const QString &source,
		const QString &basePath) {
	if (source.isEmpty()) {
		return QString();
	} else if (source.startsWith(u"file:"_q, Qt::CaseInsensitive)) {
		const auto url = QUrl(source);
		if (!url.isLocalFile()) {
			return QString();
		}
		const auto info = QFileInfo(url.toLocalFile());
		return (info.exists()
			&& info.isFile()
			&& LooksLikeMediaExtension(info.suffix()))
			? info.canonicalFilePath()
			: QString();
	} else if (basePath.isEmpty() || source.contains(u"://"_q)) {
		return QString();
	}
	const auto decoded = QUrl::fromPercentEncoding(source.toUtf8());
	const auto absolute = QDir(basePath).absoluteFilePath(decoded);
	const auto info = QFileInfo(absolute);
	if (!info.exists() || !info.isFile()) {
		return QString();
	}
	const auto canonical = info.canonicalFilePath();
	const auto root = QDir(basePath).canonicalPath();
	return (root.isEmpty() || !canonical.startsWith(root))
		? QString()
		: canonical;
}

[[nodiscard]] bool FillImportedMedia(
		not_null<Main::Session*> session,
		RichPage::Block &result,
		const TextUtilities::HtmlMedia &media,
		TextUtilities::HtmlBlockKind kind) {
	using Source = TextUtilities::HtmlBlockKind;
	result.spoiler = media.spoiler;
	result.width = media.width;
	result.height = media.height;
	result.autoplay = media.autoplay;
	result.loop = media.loop;
	if (kind == Source::Photo) {
		result.kind = RichPage::BlockKind::Photo;
		const auto id = MediaIdFromIdentity(media.identity, u"photo"_q);
		if (const auto photo = UsablePhoto(session, id)) {
			result.photoId = id;
			result.photo = photo;
			return true;
		}
		return false;
	}
	result.kind = (kind == Source::Audio)
		? RichPage::BlockKind::Audio
		: RichPage::BlockKind::Video;
	const auto id = MediaIdFromIdentity(
		media.identity,
		((kind == Source::Audio) ? u"audio"_q : u"video"_q));
	if (const auto document = UsableDocument(session, id)) {
		result.documentId = id;
		result.document = document;
		return true;
	}
	return false;
}

struct ImportContext {
	Main::Session *session = nullptr;
	QString basePath;
	std::vector<ImportedLocalMedia> localMedia;
};

[[nodiscard]] std::optional<uint64> AddImportedLocalMedia(
		ImportContext &context,
		const QString &source) {
	if (int(context.localMedia.size()) >= kImportedMediaPlaceholderLimit) {
		return std::nullopt;
	}
	auto media = ImportedLocalMedia();
	if (source.startsWith(u"data:"_q, Qt::CaseInsensitive)) {
		media.content = DataUriImageContent(source);
		if (media.content.isEmpty()) {
			return std::nullopt;
		}
	} else {
		media.path = LocalMediaPath(source, context.basePath);
		if (media.path.isEmpty()) {
			return std::nullopt;
		}
	}
	const auto placeholder = ImportedMediaPlaceholderId(
		int(context.localMedia.size()));
	context.localMedia.push_back(std::move(media));
	return placeholder;
}

[[nodiscard]] std::vector<RichPage::Block> ConvertImportedBlocks(
	std::vector<TextUtilities::HtmlBlock> blocks,
	ImportContext &context);

[[nodiscard]] RichPage::Block ConvertImportedBlock(
		TextUtilities::HtmlBlock block,
		ImportContext &context) {
	using Kind = TextUtilities::HtmlBlockKind;
	auto result = RichPage::Block();
	result.anchorId = std::move(block.anchorId);
	switch (block.kind) {
	case Kind::Paragraph:
		result.kind = RichPage::BlockKind::Paragraph;
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::Heading:
		result.kind = RichPage::BlockKind::Heading;
		result.headingLevel = std::clamp(block.headingLevel, 1, 6);
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::Divider:
		result.kind = RichPage::BlockKind::Divider;
		break;
	case Kind::Quote:
	case Kind::Pullquote:
		result.kind = RichPage::BlockKind::Quote;
		result.pullquote = (block.kind == Kind::Pullquote);
		result.text.text = ConvertImportedText(std::move(block.text));
		result.caption.text = ConvertImportedText(std::move(block.caption));
		result.blocks = ConvertImportedBlocks(
			std::move(block.children),
			context);
		break;
	case Kind::Code:
		result.kind = RichPage::BlockKind::Code;
		result.language = std::move(block.language);
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::Footer:
		result.kind = RichPage::BlockKind::Footer;
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::List: {
		result.kind = RichPage::BlockKind::List;
		result.listKind = (block.listKind
			== TextUtilities::HtmlListKind::Ordered)
			? RichPage::ListKind::Ordered
			: RichPage::ListKind::Bullet;
		if (result.listKind == RichPage::ListKind::Ordered) {
			result.orderedList = RichPage::OrderedListData{
				.reversed = block.listReversed,
				.start = block.listStart,
				.type = (block.listType.isEmpty()
					? std::optional<QString>()
					: std::make_optional(block.listType)),
			};
		}
		result.listItems.reserve(block.items.size());
		for (auto &item : block.items) {
			auto converted = RichPage::ListItem();
			converted.taskState = ConvertTaskState(item.taskState);
			converted.number = RichPage::OrderedListItemData{
				.value = item.value,
			};
			converted.anchorId = std::move(item.anchorId);
			converted.text.text = ConvertImportedText(std::move(item.text));
			converted.blocks = ConvertImportedBlocks(
				std::move(item.blocks),
				context);
			result.listItems.push_back(std::move(converted));
		}
	} break;
	case Kind::Details:
		result.kind = RichPage::BlockKind::Details;
		result.open = block.detailsOpen;
		result.text.text = ConvertImportedText(std::move(block.text));
		result.blocks = ConvertImportedBlocks(
			std::move(block.children),
			context);
		if (result.blocks.empty()) {
			auto paragraph = RichPage::Block();
			paragraph.kind = RichPage::BlockKind::Paragraph;
			result.blocks.push_back(std::move(paragraph));
		}
		break;
	case Kind::Table:
		if (block.table) {
			result = ConvertTable(*block.table);
		}
		break;
	case Kind::Photo:
	case Kind::Video:
	case Kind::Audio:
		if (!FillImportedMedia(
				context.session,
				result,
				block.media,
				block.kind)) {
			const auto placeholder = AddImportedLocalMedia(
				context,
				block.media.source);
			if (!placeholder) {
				return RichPage::Block();
			} else if (result.kind == RichPage::BlockKind::Photo) {
				result.photoId = *placeholder;
			} else {
				result.documentId = *placeholder;
			}
		}
		result.caption.text = ConvertImportedText(std::move(block.caption));
		break;
	case Kind::Collage:
	case Kind::Slideshow: {
		result.kind = RichPage::BlockKind::GroupedMedia;
		result.mediaIntent = (block.kind == Kind::Slideshow)
			? RichPage::GroupedMediaIntent::Slideshow
			: RichPage::GroupedMediaIntent::Collage;
		for (auto &child : block.children) {
			auto media = RichPage::Block();
			if (!FillImportedMedia(
					context.session,
					media,
					child.media,
					child.kind)) {
				const auto placeholder = AddImportedLocalMedia(
					context,
					child.media.source);
				if (!placeholder) {
					continue;
				} else if (media.kind == RichPage::BlockKind::Photo) {
					media.photoId = *placeholder;
				} else {
					media.documentId = *placeholder;
				}
			}
			result.mediaItems.push_back({
				.kind = media.kind,
				.photo = media.photo,
				.document = media.document,
				.photoId = media.photoId,
				.documentId = media.documentId,
				.width = media.width,
				.height = media.height,
				.autoplay = media.autoplay,
				.loop = media.loop,
				.spoiler = media.spoiler,
			});
		}
		if (result.mediaItems.empty()) {
			return RichPage::Block();
		}
		result.caption.text = ConvertImportedText(std::move(block.caption));
	} break;
	case Kind::Map: {
		auto latitudeOk = false;
		auto longitudeOk = false;
		const auto latitude = block.mapPoint.latitude.toDouble(
			&latitudeOk);
		const auto longitude = block.mapPoint.longitude.toDouble(
			&longitudeOk);
		if (!latitudeOk || !longitudeOk) {
			return RichPage::Block();
		}
		result.kind = RichPage::BlockKind::Map;
		result.latitude = latitude;
		result.longitude = longitude;
		result.zoom = block.mapPoint.zoom;
		result.caption.text = ConvertImportedText(std::move(block.caption));
	} break;
	case Kind::Math:
		result.kind = RichPage::BlockKind::Math;
		result.formula = block.formula.isEmpty()
			? block.text.text
			: block.formula;
		if (result.formula.isEmpty()) {
			return RichPage::Block();
		}
		break;
	}
	return result;
}

std::vector<RichPage::Block> ConvertImportedBlocks(
		std::vector<TextUtilities::HtmlBlock> blocks,
		ImportContext &context) {
	auto result = std::vector<RichPage::Block>();
	result.reserve(blocks.size());
	for (auto &block : blocks) {
		auto converted = ConvertImportedBlock(std::move(block), context);
		if (converted.kind != RichPage::BlockKind::Unsupported) {
			result.push_back(std::move(converted));
		}
	}
	return result;
}

[[nodiscard]] TextUtilities::HtmlBlocksLimits BlocksImportLimitsFor(
		const RichMessageLimits &limits,
		int usedBlocks) {
	const auto blocks = limits.maxBlocks - usedBlocks - 1;
	const auto columns = limits.maxTableCols;
	if (blocks <= 0 || columns <= 0) {
		return { .maxBlocks = 0 };
	}
	const auto cells = std::min(
		int64(blocks) * columns,
		int64(std::numeric_limits<int>::max()));
	return {
		.maxBlocks = blocks,
		.maxDepth = limits.maxDepth,
		.maxBlockLength = limits.lengthLimit,
		.maxTotalLength = limits.lengthLimit,
		.table = {
			.maxRows = blocks,
			.maxColumns = columns,
			.maxCells = int(cells),
			.maxCellLength = kMaxCellLength,
		},
	};
}

[[nodiscard]] std::vector<std::vector<QString>> ParseDelimited(
		const QString &text,
		QChar delimiter,
		const TableImportLimits &limits,
		bool *truncated) {
	auto result = std::vector<std::vector<QString>>();
	auto row = std::vector<QString>();
	auto field = QString();
	auto quoted = false;
	auto cells = 0;
	const auto appendToField = [&](QChar ch) {
		if (field.size() < limits.maxCellLength) {
			field.append(ch);
		} else {
			*truncated = true;
		}
	};
	const auto finishField = [&] {
		row.push_back(base::take(field));
		++cells;
	};
	const auto finishRow = [&] {
		finishField();
		result.push_back(base::take(row));
	};
	for (auto i = 0, size = int(text.size()); i != size; ++i) {
		const auto ch = text[i];
		if (quoted) {
			if (ch != '"') {
				appendToField(ch);
			} else if (i + 1 != size && text[i + 1] == '"') {
				appendToField(ch);
				++i;
			} else {
				quoted = false;
			}
			continue;
		} else if (ch == '"' && field.isEmpty()) {
			quoted = true;
			continue;
		} else if (ch == delimiter) {
			finishField();
			if (cells >= limits.maxCells) {
				*truncated = true;
				result.push_back(base::take(row));
				return result;
			}
			continue;
		} else if (ch != '\r' && ch != '\n') {
			appendToField(ch);
			continue;
		}
		if (ch == '\r' && i + 1 != size && text[i + 1] == '\n') {
			++i;
		}
		finishRow();
		if ((int(result.size()) >= limits.maxRows)
			|| (cells >= limits.maxCells)) {
			if (i + 1 != size) {
				*truncated = true;
			}
			return result;
		}
	}
	if (!field.isEmpty() || !row.empty()) {
		finishRow();
	}
	return result;
}

[[nodiscard]] std::optional<TableImportResult> TableFromDelimitedText(
		const QString &text,
		const TableImportLimits &limits) {
	auto truncated = false;
	auto rows = ParseDelimited(text, u'\t', limits, &truncated);
	if (rows.empty()) {
		return std::nullopt;
	}
	auto columns = 0;
	for (const auto &row : rows) {
		columns = std::max(columns, int(row.size()));
	}
	if (columns > limits.maxColumns) {
		columns = limits.maxColumns;
		truncated = true;
	}
	if (columns < 1 || (rows.size() < 2 && columns < 2)) {
		return std::nullopt;
	}
	auto block = RichPage::Block();
	block.kind = RichPage::BlockKind::Table;
	block.bordered = true;
	block.tableRows.reserve(rows.size());
	for (auto &fields : rows) {
		auto row = RichPage::TableRow();
		row.cells.reserve(columns);
		for (auto i = 0; i != columns; ++i) {
			auto cell = RichPage::TableCell();
			if (i < int(fields.size())) {
				cell.text.text.text = std::move(fields[i]);
			}
			row.cells.push_back(std::move(cell));
		}
		block.tableRows.push_back(std::move(row));
	}
	return TableImportResult{
		.block = std::move(block),
		.truncated = truncated,
	};
}

[[nodiscard]] QString ExportedHtmlFilePath(not_null<const QMimeData*> data) {
	if (!data->hasUrls()) {
		return QString();
	}
	const auto urls = data->urls();
	if (urls.size() != 1 || !urls.front().isLocalFile()) {
		return QString();
	}
	const auto path = urls.front().toLocalFile();
	if (!path.endsWith(u".html"_q, Qt::CaseInsensitive)) {
		return QString();
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const auto head = QString::fromUtf8(file.read(2048));
	return head.contains(RichExportGeneratorMarker())
		? path
		: QString();
}

[[nodiscard]] std::optional<BlocksImportResult> BlocksFromHtmlSource(
		not_null<Main::Session*> session,
		const QString &html,
		const QString &basePath,
		const RichMessageLimits &limits,
		int usedBlocks) {
	auto parsed = TextUtilities::BlocksFromHtml(
		html,
		BlocksImportLimitsFor(limits, usedBlocks));
	if (!parsed) {
		return std::nullopt;
	}
	auto context = ImportContext{
		.session = session,
		.basePath = basePath,
	};
	auto blocks = ConvertImportedBlocks(std::move(parsed->blocks), context);
	if (blocks.empty() && context.localMedia.empty()) {
		return std::nullopt;
	}
	return BlocksImportResult{
		.blocks = std::move(blocks),
		.localMedia = std::move(context.localMedia),
		.truncated = parsed->truncated,
	};
}

// SplitTextIntoRichPage() spells field text with exactly these kinds.
[[nodiscard]] bool BlockKindFitsComposeField(RichPage::BlockKind kind) {
	using Kind = RichPage::BlockKind;
	return (kind == Kind::Paragraph)
		|| (kind == Kind::Code)
		|| (kind == Kind::Quote);
}

} // namespace

TableImportLimits TableImportLimitsFor(
		const RichMessageLimits &limits,
		int usedBlocks) {
	const auto rows = limits.maxBlocks - usedBlocks - 1;
	const auto columns = limits.maxTableCols;
	if (rows <= 0 || columns <= 0) {
		return TableImportLimits();
	}
	const auto cells = std::min(
		int64(rows) * columns,
		int64(std::numeric_limits<int>::max()));
	return {
		.maxRows = rows,
		.maxColumns = columns,
		.maxCells = int(cells),
		.maxCellLength = kMaxCellLength,
	};
}

bool MimeDataLooksLikeTable(not_null<const QMimeData*> data) {
	if (data->hasHtml()) {
		return TextUtilities::HtmlContainsTable(data->html());
	}
	return data->hasText()
		&& HasSpreadsheetHint(data)
		&& data->text().contains(u'\t');
}

std::optional<TableImportResult> TableFromMimeData(
		not_null<const QMimeData*> data,
		const TableImportLimits &limits) {
	if ((limits.maxRows <= 0)
		|| (limits.maxColumns <= 0)
		|| (limits.maxCells <= 0)) {
		return std::nullopt;
	}
	if (data->hasHtml()) {
		const auto html = data->html();
		auto parsed = TextUtilities::TableFromHtml(html, {
			.maxRows = limits.maxRows,
			.maxColumns = limits.maxColumns,
			.maxCells = limits.maxCells,
			.maxCellLength = limits.maxCellLength,
		});
		if (!parsed) {
			return std::nullopt;
		} else if (parsed->rows.size() < 2 && parsed->columns < 2) {
			return std::nullopt;
		} else if (!TableCoversWholeHtml(html, *parsed)) {
			return std::nullopt;
		}
		return TableImportResult{
			.block = ConvertTable(*parsed),
			.truncated = parsed->truncated,
		};
	} else if (!data->hasText() || !HasSpreadsheetHint(data)) {
		return std::nullopt;
	}
	return TableFromDelimitedText(data->text(), limits);
}

bool MimeDataLooksLikeExportedHtml(not_null<const QMimeData*> data) {
	return !ExportedHtmlFilePath(data).isEmpty();
}

bool RichBlocksCarryStructure(const std::vector<RichPage::Block> &blocks) {
	for (const auto &block : blocks) {
		if (!BlockKindFitsComposeField(block.kind)
			|| RichBlocksCarryStructure(block.blocks)) {
			return true;
		}
	}
	return false;
}

bool MimeDataHasRichStructure(
		not_null<Main::Session*> session,
		not_null<const QMimeData*> data,
		const RichMessageLimits &limits) {
	if (data->hasFormat(ClipboardMimeType())) {
		return true;
	}
	const auto imported = BlocksFromMimeData(session, data, limits, 0);
	return imported && RichBlocksCarryStructure(imported->blocks);
}

bool TextHasMarkdownStructure(
		const QString &text,
		const RichMessageLimits &limits) {
	const auto imported = BlocksFromMarkdown(text, limits, 0);
	return imported && RichBlocksCarryStructure(imported->blocks);
}

std::optional<BlocksImportResult> BlocksFromMimeData(
		not_null<Main::Session*> session,
		not_null<const QMimeData*> data,
		const RichMessageLimits &limits,
		int usedBlocks) {
	if (const auto path = ExportedHtmlFilePath(data); !path.isEmpty()) {
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return std::nullopt;
		}
		return BlocksFromHtmlSource(
			session,
			QString::fromUtf8(file.readAll()),
			QFileInfo(path).absolutePath(),
			limits,
			usedBlocks);
	} else if (!data->hasHtml()) {
		return std::nullopt;
	}
	return BlocksFromHtmlSource(
		session,
		data->html(),
		QString(),
		limits,
		usedBlocks);
}

namespace {

constexpr auto kMaxMarkdownImportLength = 256 * 1024;
constexpr auto kMaxMarkdownQuoteDepth = 8;

struct MarkdownInlineState {
	TextWithTags text;
	bool marked = false;
};

[[nodiscard]] QString WithMarkdownTag(const QString &tag, QStringView added) {
	return TextUtilities::TagWithAdded(tag, added.toString());
}

void AppendMarkdownRun(
		MarkdownInlineState &state,
		QStringView text,
		const QString &tag) {
	if (text.isEmpty()) {
		return;
	}
	const auto offset = int(state.text.text.size());
	state.text.text.append(text);
	if (!tag.isEmpty()) {
		state.text.tags.push_back({ offset, int(text.size()), tag });
	}
}

[[nodiscard]] bool MarkdownEscapable(QChar ch) {
	switch (ch.unicode()) {
	case '\\':
	case '*':
	case '_':
	case '~':
	case '|':
	case '`':
	case '[':
	case ']':
	case '(':
	case ')':
	case '#':
	case '!':
	case '-':
	case '.':
	case '>':
		return true;
	default:
		return false;
	}
}

[[nodiscard]] QString MarkdownLinkUrl(QStringView inside) {
	auto url = inside.trimmed();
	const auto space = url.indexOf(u' ');
	if (space >= 0) {
		url = url.mid(0, space);
	}
	if (url.isEmpty()) {
		return QString();
	} else if (url.startsWith(u"www.")) {
		return u"https://"_q + url.toString();
	}
	const auto scheme = url.indexOf(u"://");
	return (scheme > 0 || url.startsWith(u"mailto:"))
		? url.toString()
		: QString();
}

[[nodiscard]] int FindMarkdownLinkClose(QStringView text, int from) {
	auto depth = 0;
	for (auto i = from; i < text.size(); ++i) {
		const auto ch = text[i];
		if (ch == u'\\') {
			++i;
		} else if (ch == u'(') {
			++depth;
		} else if (ch == u')') {
			if (!depth) {
				return i;
			}
			--depth;
		}
	}
	return -1;
}

[[nodiscard]] int FindMarkdownCloser(
		QStringView text,
		int from,
		QStringView marker,
		bool wordBounded) {
	for (auto i = from; i + marker.size() <= text.size(); ++i) {
		if (text.mid(i, marker.size()) != marker
			|| text[i - 1].isSpace()) {
			continue;
		} else if (wordBounded
			&& (i + marker.size() < text.size())
			&& text[i + marker.size()].isLetterOrNumber()) {
			continue;
		}
		return i;
	}
	return -1;
}

void ParseMarkdownInline(
		MarkdownInlineState &state,
		QStringView text,
		const QString &tag) {
	struct Marker {
		QStringView marker;
		QStringView applied;
		bool wordBounded = false;
		QStringView appliedNested;
	};
	static const auto kMarkers = std::array{
		Marker{ u"***", u"**", false, u"__" },
		Marker{ u"___", u"**", true, u"__" },
		Marker{ u"**", u"**" },
		Marker{ u"__", u"**", true },
		Marker{ u"~~", u"~~" },
		Marker{ u"||", u"||" },
		Marker{ u"*", u"__", true },
		Marker{ u"_", u"__", true },
	};
	auto literalFrom = 0;
	auto i = 0;
	const auto flush = [&](int till) {
		AppendMarkdownRun(
			state,
			text.mid(literalFrom, till - literalFrom),
			tag);
	};
	while (i < text.size()) {
		const auto ch = text[i];
		if (ch == u'\\'
			&& (i + 1 < text.size())
			&& MarkdownEscapable(text[i + 1])) {
			flush(i);
			AppendMarkdownRun(state, text.mid(i + 1, 1), tag);
			state.marked = true;
			i += 2;
			literalFrom = i;
			continue;
		} else if (ch == u'`') {
			const auto closer = text.indexOf(u'`', i + 1);
			if (closer > i + 1) {
				flush(i);
				AppendMarkdownRun(
					state,
					text.mid(i + 1, closer - i - 1),
					WithMarkdownTag(tag, u"`"));
				state.marked = true;
				i = closer + 1;
				literalFrom = i;
				continue;
			}
		} else if ((ch == u'<')
			&& (text.mid(i + 1).indexOf(u'>') > 0)) {
			const auto closer = text.indexOf(u'>', i + 1);
			const auto url = MarkdownLinkUrl(text.mid(i + 1, closer - i - 1));
			if (!url.isEmpty()) {
				flush(i);
				AppendMarkdownRun(
					state,
					text.mid(i + 1, closer - i - 1),
					WithMarkdownTag(tag, url));
				state.marked = true;
				i = closer + 1;
				literalFrom = i;
				continue;
			}
		} else if ((ch == u'[')
			|| ((ch == u'!')
				&& (i + 1 < text.size())
				&& (text[i + 1] == u'['))) {
			const auto open = (ch == u'!') ? (i + 1) : i;
			const auto closeBracket = text.indexOf(u"](", open + 1);
			const auto closeParen = (closeBracket > open)
				? FindMarkdownLinkClose(text, closeBracket + 2)
				: -1;
			if (closeParen > closeBracket + 2) {
				const auto url = MarkdownLinkUrl(text.mid(
					closeBracket + 2,
					closeParen - closeBracket - 2));
				const auto inner = text.mid(
					open + 1,
					closeBracket - open - 1);
				if (!url.isEmpty() && !inner.isEmpty()) {
					flush(i);
					ParseMarkdownInline(
						state,
						inner,
						WithMarkdownTag(tag, url));
					state.marked = true;
					i = closeParen + 1;
					literalFrom = i;
					continue;
				}
			}
		} else {
			auto matched = false;
			for (const auto &marker : kMarkers) {
				if (text.mid(i, marker.marker.size()) != marker.marker) {
					continue;
				} else if (marker.wordBounded
					&& (i > 0)
					&& text[i - 1].isLetterOrNumber()) {
					continue;
				}
				const auto contentFrom = i + int(marker.marker.size());
				if (contentFrom >= text.size()
					|| text[contentFrom].isSpace()) {
					continue;
				}
				const auto closer = FindMarkdownCloser(
					text,
					contentFrom + 1,
					marker.marker,
					marker.wordBounded);
				if (closer < 0) {
					continue;
				}
				auto applied = WithMarkdownTag(tag, marker.applied);
				if (!marker.appliedNested.isEmpty()) {
					applied = WithMarkdownTag(applied, marker.appliedNested);
				}
				flush(i);
				ParseMarkdownInline(
					state,
					text.mid(contentFrom, closer - contentFrom),
					applied);
				state.marked = true;
				i = closer + int(marker.marker.size());
				literalFrom = i;
				matched = true;
				break;
			}
			if (matched) {
				continue;
			}
		}
		++i;
	}
	flush(int(text.size()));
}

[[nodiscard]] TextWithTags MarkdownInlineText(
		QStringView line,
		bool *marked) {
	auto state = MarkdownInlineState();
	ParseMarkdownInline(state, line, QString());
	if (state.marked && marked) {
		*marked = true;
	}
	return std::move(state.text);
}

struct MarkdownListMarker {
	int indent = 0;
	bool ordered = false;
	int number = 1;
	RichPage::TaskState task = RichPage::TaskState::None;
	int contentFrom = 0;
};

[[nodiscard]] std::optional<MarkdownListMarker> ParseMarkdownListMarker(
		QStringView line) {
	auto result = MarkdownListMarker();
	auto i = 0;
	while (i < line.size() && (line[i] == u' ' || line[i] == u'\t')) {
		result.indent += (line[i] == u'\t') ? 4 : 1;
		++i;
	}
	if (i >= line.size()) {
		return std::nullopt;
	}
	const auto ch = line[i];
	if (ch == u'-' || ch == u'*' || ch == u'+') {
		++i;
	} else if (ch.isDigit()) {
		auto digits = 0;
		auto value = 0;
		while (i < line.size() && line[i].isDigit() && digits < 9) {
			value = value * 10 + line[i].digitValue();
			++i;
			++digits;
		}
		if (i >= line.size() || (line[i] != u'.' && line[i] != u')')) {
			return std::nullopt;
		}
		++i;
		result.ordered = true;
		result.number = value;
	} else {
		return std::nullopt;
	}
	if (i >= line.size() || line[i] != u' ') {
		return std::nullopt;
	}
	while (i < line.size() && line[i] == u' ') {
		++i;
	}
	if ((i + 2 < line.size())
		&& (line[i] == u'[')
		&& (line[i + 2] == u']')
		&& ((i + 3 == line.size()) || (line[i + 3] == u' '))) {
		const auto mark = line[i + 1];
		if (mark == u' ' || mark == u'x' || mark == u'X') {
			result.task = (mark == u' ')
				? RichPage::TaskState::Unchecked
				: RichPage::TaskState::Checked;
			i += 3;
			while (i < line.size() && line[i] == u' ') {
				++i;
			}
		}
	}
	result.contentFrom = i;
	return result;
}

[[nodiscard]] bool MarkdownDividerLine(QStringView trimmed) {
	if (trimmed.size() < 3) {
		return false;
	}
	const auto ch = trimmed[0];
	if (ch != u'-' && ch != u'*' && ch != u'_') {
		return false;
	}
	auto count = 0;
	for (const auto c : trimmed) {
		if (c == ch) {
			++count;
		} else if (c != u' ') {
			return false;
		}
	}
	return count >= 3;
}

[[nodiscard]] bool MarkdownSetextH1Line(QStringView trimmed) {
	if (trimmed.isEmpty()) {
		return false;
	}
	for (const auto ch : trimmed) {
		if (ch != u'=') {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool MarkdownTableSeparatorLine(QStringView trimmed) {
	if (!trimmed.contains(u'-') || !trimmed.contains(u'|')) {
		return false;
	}
	for (const auto ch : trimmed) {
		if (ch != u'-' && ch != u'|' && ch != u':' && ch != u' ') {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QList<QStringView> SplitMarkdownTableRow(QStringView line) {
	auto trimmed = line.trimmed();
	if (trimmed.startsWith(u'|')) {
		trimmed = trimmed.mid(1);
	}
	if (trimmed.endsWith(u'|') && !trimmed.endsWith(u"\\|")) {
		trimmed = trimmed.mid(0, trimmed.size() - 1);
	}
	auto result = QList<QStringView>();
	auto from = 0;
	for (auto i = 0; i <= trimmed.size(); ++i) {
		if (i == trimmed.size()
			|| (trimmed[i] == u'|' && (i == 0 || trimmed[i - 1] != u'\\'))) {
			result.push_back(trimmed.mid(from, i - from).trimmed());
			from = i + 1;
		}
	}
	return result;
}

[[nodiscard]] RichPage::TableAlignment MarkdownCellAlignment(
		QStringView separator) {
	const auto left = separator.startsWith(u':');
	const auto right = separator.endsWith(u':');
	return (left && right)
		? RichPage::TableAlignment::Center
		: right
		? RichPage::TableAlignment::Right
		: RichPage::TableAlignment::Left;
}

struct MarkdownBlocksBuilder {
	int budget = 0;
	bool truncated = false;
	bool marked = false;

	[[nodiscard]] bool allot() {
		if (budget > 0) {
			--budget;
			return true;
		}
		truncated = true;
		return false;
	}
};

void ParseMarkdownBlocks(
	MarkdownBlocksBuilder &builder,
	std::vector<RichPage::Block> &out,
	QStringView text,
	int depth);

[[nodiscard]] RichPage::Block MarkdownParagraph(
		MarkdownBlocksBuilder &builder,
		QStringView line) {
	auto block = RichPage::Block();
	block.kind = RichPage::BlockKind::Paragraph;
	block.text.text = ConvertImportedText(
		MarkdownInlineText(line, &builder.marked));
	return block;
}

void ParseMarkdownBlocks(
		MarkdownBlocksBuilder &builder,
		std::vector<RichPage::Block> &out,
		QStringView text,
		int depth) {
	auto lines = QList<QStringView>();
	auto from = 0;
	for (auto i = 0; i <= text.size(); ++i) {
		if (i == text.size() || text[i] == u'\n') {
			auto line = text.mid(from, i - from);
			if (line.endsWith(u'\r')) {
				line = line.mid(0, line.size() - 1);
			}
			lines.push_back(line);
			from = i + 1;
		}
	}
	auto index = 0;
	const auto count = int(lines.size());
	while (index != count) {
		if (builder.truncated) {
			return;
		}
		const auto line = lines[index];
		const auto trimmed = line.trimmed();
		if (trimmed.isEmpty()) {
			++index;
			continue;
		}
		const auto fence = trimmed.startsWith(u"```");
		if (fence) {
			const auto language = trimmed.mid(3).trimmed().toString();
			auto content = QStringList();
			++index;
			while (index != count
				&& !lines[index].trimmed().startsWith(u"```")) {
				content.push_back(lines[index].toString());
				++index;
			}
			if (index != count) {
				++index;
			}
			if (!builder.allot()) {
				return;
			}
			auto block = RichPage::Block();
			block.kind = RichPage::BlockKind::Code;
			block.language = language;
			block.text.text.text = content.join(u'\n');
			out.push_back(std::move(block));
			builder.marked = true;
			continue;
		}
		auto headingLevel = 0;
		while (headingLevel < trimmed.size()
			&& (headingLevel < 6)
			&& (trimmed[headingLevel] == u'#')) {
			++headingLevel;
		}
		if (headingLevel > 0
			&& headingLevel < trimmed.size()
			&& trimmed[headingLevel] == u' ') {
			if (!builder.allot()) {
				return;
			}
			auto block = RichPage::Block();
			block.kind = RichPage::BlockKind::Heading;
			block.headingLevel = headingLevel;
			block.text.text = ConvertImportedText(MarkdownInlineText(
				trimmed.mid(headingLevel + 1).trimmed(),
				&builder.marked));
			out.push_back(std::move(block));
			builder.marked = true;
			++index;
			continue;
		}
		if (MarkdownDividerLine(trimmed)) {
			if (!builder.allot()) {
				return;
			}
			auto block = RichPage::Block();
			block.kind = RichPage::BlockKind::Divider;
			out.push_back(std::move(block));
			builder.marked = true;
			++index;
			continue;
		}
		if (trimmed.startsWith(u'>') && depth < kMaxMarkdownQuoteDepth) {
			auto inner = QStringList();
			while (index != count) {
				const auto quoteLine = lines[index].trimmed();
				if (!quoteLine.startsWith(u'>')) {
					break;
				}
				auto stripped = quoteLine.mid(1);
				if (stripped.startsWith(u' ')) {
					stripped = stripped.mid(1);
				}
				inner.push_back(stripped.toString());
				++index;
			}
			if (!builder.allot()) {
				return;
			}
			auto blocks = std::vector<RichPage::Block>();
			ParseMarkdownBlocks(
				builder,
				blocks,
				inner.join(u'\n'),
				depth + 1);
			auto block = RichPage::Block();
			block.kind = RichPage::BlockKind::Quote;
			if ((blocks.size() == 1)
				&& (blocks.front().kind == RichPage::BlockKind::Paragraph)) {
				block.text = std::move(blocks.front().text);
			} else {
				block.blocks = std::move(blocks);
			}
			out.push_back(std::move(block));
			builder.marked = true;
			continue;
		}
		if (trimmed.contains(u'|')
			&& (index + 1 != count)
			&& MarkdownTableSeparatorLine(lines[index + 1].trimmed())) {
			const auto header = SplitMarkdownTableRow(line);
			const auto separators = SplitMarkdownTableRow(lines[index + 1]);
			if (!header.isEmpty()
				&& (separators.size() == header.size())) {
				if (!builder.allot()) {
					return;
				}
				auto block = RichPage::Block();
				block.kind = RichPage::BlockKind::Table;
				block.bordered = true;
				const auto appendRow = [&](
						const QList<QStringView> &cells,
						bool isHeader) {
					auto row = RichPage::TableRow();
					row.cells.reserve(cells.size());
					for (auto i = 0; i != cells.size(); ++i) {
						const auto full = cells[i].toString().replace(
							u"\\|"_q,
							u"|"_q);
						row.cells.push_back({
							.text = {
								.text = ConvertImportedText(
									MarkdownInlineText(
										QStringView(full)
											.left(kMaxCellLength),
										&builder.marked)),
							},
							.header = isHeader,
							.alignment = (i < separators.size())
								? MarkdownCellAlignment(separators[i])
								: RichPage::TableAlignment::Left,
						});
					}
					block.tableRows.push_back(std::move(row));
				};
				appendRow(header, true);
				index += 2;
				while (index != count) {
					const auto rowLine = lines[index].trimmed();
					if (!rowLine.contains(u'|')) {
						break;
					}
					auto cells = SplitMarkdownTableRow(lines[index]);
					while (cells.size() > header.size()) {
						cells.removeLast();
					}
					while (cells.size() < header.size()) {
						cells.push_back(QStringView());
					}
					appendRow(cells, false);
					++index;
				}
				out.push_back(std::move(block));
				builder.marked = true;
				continue;
			}
		}
		if (ParseMarkdownListMarker(line)) {
			auto stack = std::vector<std::pair<int, RichPage::Block*>>();
			const auto startList = [&](
					const MarkdownListMarker &item)
			-> RichPage::Block* {
				auto block = RichPage::Block();
				block.kind = RichPage::BlockKind::List;
				block.listKind = item.ordered
					? RichPage::ListKind::Ordered
					: RichPage::ListKind::Bullet;
				if (item.ordered && item.number != 1) {
					block.orderedList.start = item.number;
				}
				if (stack.empty()) {
					out.push_back(std::move(block));
					return &out.back();
				}
				auto &parent = *stack.back().second;
				auto &owner = parent.listItems.back();
				if (!owner.text.text.text.isEmpty()) {
					auto paragraph = RichPage::Block();
					paragraph.kind = RichPage::BlockKind::Paragraph;
					paragraph.text = std::move(owner.text);
					owner.text = RichPage::RichText();
					owner.blocks.push_back(std::move(paragraph));
				}
				owner.blocks.push_back(std::move(block));
				return &owner.blocks.back();
			};
			while (index != count) {
				const auto itemLine = lines[index];
				const auto item = ParseMarkdownListMarker(itemLine);
				if (!item) {
					break;
				}
				while (!stack.empty()
					&& (item->indent < stack.back().first)) {
					stack.pop_back();
				}
				const auto sameLevel = !stack.empty()
					&& (item->indent == stack.back().first);
				if (sameLevel) {
					const auto list = stack.back().second;
					const auto ordered = (list->listKind
						== RichPage::ListKind::Ordered);
					if (ordered != item->ordered) {
						if (stack.size() > 1) {
							stack.pop_back();
						} else {
							break;
						}
					}
				}
				if (stack.empty()
					|| (item->indent > stack.back().first)) {
					if (!builder.allot()) {
						return;
					}
					const auto list = startList(*item);
					stack.push_back({ item->indent, list });
				}
				auto &list = *stack.back().second;
				auto entry = RichPage::ListItem();
				entry.taskState = item->task;
				entry.text.text = ConvertImportedText(MarkdownInlineText(
					itemLine.mid(item->contentFrom),
					&builder.marked));
				list.listItems.push_back(std::move(entry));
				builder.marked = true;
				++index;
			}
			continue;
		}
		if ((index + 1 != count)
			&& MarkdownSetextH1Line(lines[index + 1].trimmed())) {
			if (!builder.allot()) {
				return;
			}
			auto block = RichPage::Block();
			block.kind = RichPage::BlockKind::Heading;
			block.headingLevel = 1;
			block.text.text = ConvertImportedText(
				MarkdownInlineText(trimmed, &builder.marked));
			out.push_back(std::move(block));
			builder.marked = true;
			index += 2;
			continue;
		}
		if (!builder.allot()) {
			return;
		}
		out.push_back(MarkdownParagraph(builder, trimmed));
		++index;
	}
}

} // namespace

std::optional<BlocksImportResult> BlocksFromMarkdown(
		const QString &text,
		const RichMessageLimits &limits,
		int usedBlocks) {
	const auto budget = limits.maxBlocks - usedBlocks - 1;
	if (text.isEmpty()
		|| (text.size() > kMaxMarkdownImportLength)
		|| (text.size() > limits.lengthLimit)
		|| (budget <= 0)) {
		return std::nullopt;
	}
	auto builder = MarkdownBlocksBuilder{
		.budget = budget,
	};
	auto blocks = std::vector<RichPage::Block>();
	ParseMarkdownBlocks(builder, blocks, text, 0);
	if (blocks.empty() || !builder.marked) {
		return std::nullopt;
	}
	return BlocksImportResult{
		.blocks = std::move(blocks),
		.truncated = builder.truncated,
	};
}

std::optional<ClipboardData> BlockClipboardDataFromRichText(
		TextWithEntities text) {
	const auto isBlockEntity = [](const EntityInText &entity) {
		const auto type = entity.type();
		return (type == EntityType::Pre)
			|| (type == EntityType::Blockquote);
	};
	if (!ranges::any_of(text.entities, isBlockEntity)) {
		return std::nullopt;
	}
	auto page = SplitTextIntoRichPage(std::move(text));
	if (page.blocks.empty()) {
		return std::nullopt;
	}
	auto result = ClipboardBlockData();
	result.blocks = std::move(page.blocks);
	return ClipboardData(std::move(result));
}

std::optional<ClipboardData> BlockClipboardDataFromFieldTags(
		not_null<const QMimeData*> data) {
	const auto textMime = TextUtilities::TagsTextMimeType();
	const auto tagsMime = TextUtilities::TagsMimeType();
	if (!data->hasFormat(textMime) || !data->hasFormat(tagsMime)) {
		return std::nullopt;
	}
	auto text = QString::fromUtf8(data->data(textMime));
	const auto tags = TextUtilities::DeserializeTags(
		data->data(tagsMime),
		int(text.size()));
	auto entities = TextUtilities::ConvertTextTagsToEntities(tags);
	return BlockClipboardDataFromRichText({
		std::move(text),
		std::move(entities),
	});
}

std::optional<Ui::PreparedList> PreparedMediaFromClipboard(
		not_null<const QMimeData*> data,
		bool premium) {
	const auto hasImage = data->hasImage();
	const auto urls = Core::ReadMimeUrls(data);
	if (!urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize,
			premium);
		if (list.error != Ui::PreparedList::Error::NonLocalUrl) {
			return list;
		} else if (!hasImage) {
			return std::nullopt;
		}
	}
	if (auto read = Core::ReadMimeImage(data)) {
		return Storage::PrepareMediaFromImage(
			std::move(read.image),
			std::move(read.content),
			st::sendMediaPreviewSize);
	}
	return std::nullopt;
}

bool IsAcceptableDropMedia(not_null<const QMimeData*> data) {
	if (data->hasFormat(u"application/x-td-forward"_q)) {
		return false;
	} else if (data->hasImage()) {
		return true;
	}
	const auto urls = Core::ReadMimeUrls(data);
	if (urls.isEmpty()) {
		return false;
	}
	for (const auto &url : urls) {
		if (!url.isLocalFile()
			|| QFileInfo(Platform::File::UrlToLocal(url)).isDir()) {
			return false;
		}
	}
	return true;
}

bool CanPrepareMediaFromClipboard(not_null<const QMimeData*> data) {
	return IsAcceptableDropMedia(data);
}

} // namespace Iv::Editor

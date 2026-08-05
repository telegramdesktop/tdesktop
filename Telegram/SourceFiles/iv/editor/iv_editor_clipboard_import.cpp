/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_clipboard_import.h"

#include "iv/editor/iv_editor_text_entities.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_html_tags.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QUrlQuery>

#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <array>

namespace Iv::Editor {
namespace {

constexpr auto kMaxCellLength = 4096;

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

[[nodiscard]] RichPage::Block ConvertTable(
		const TextUtilities::HtmlTable &table) {
	auto result = RichPage::Block();
	result.kind = RichPage::BlockKind::Table;
	result.bordered = true;
	result.text.text = ConvertImportedText(table.caption);
	result.tableRows.reserve(table.rows.size());
	for (const auto &row : table.rows) {
		auto converted = RichPage::TableRow();
		converted.cells.reserve(row.cells.size());
		for (const auto &cell : row.cells) {
			converted.cells.push_back({
				.text = {
					.text = {
						cell.text.text,
						TextUtilities::ConvertTextTagsToEntities(
							cell.text.tags),
					},
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

[[nodiscard]] PhotoData *UsablePhoto(
		not_null<Main::Session*> session,
		uint64 id) {
	if (!id) {
		return nullptr;
	}
	const auto photo = session->data().photo(PhotoId(id));
	if (photo->isNull() || photo->fileReference().isEmpty()) {
		return nullptr;
	}
	const auto input = photo->mtpInput();
	return (input.type() == mtpc_inputPhoto
		&& input.c_inputPhoto().vaccess_hash().v)
		? photo.get()
		: nullptr;
}

[[nodiscard]] DocumentData *UsableDocument(
		not_null<Main::Session*> session,
		uint64 id) {
	if (!id) {
		return nullptr;
	}
	const auto document = session->data().document(DocumentId(id));
	if (document->isNull()
		|| !document->hasRemoteLocation()
		|| document->fileReference().isEmpty()) {
		return nullptr;
	}
	const auto input = document->mtpInput();
	return (input.type() == mtpc_inputDocument
		&& input.c_inputDocument().vaccess_hash().v)
		? document.get()
		: nullptr;
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
	QStringList localMediaPaths;
};

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
			const auto local = LocalMediaPath(
				block.media.source,
				context.basePath);
			if (local.isEmpty()) {
				return RichPage::Block();
			}
			const auto placeholder = ImportedMediaPlaceholderId(
				int(context.localMediaPaths.size()));
			context.localMediaPaths.push_back(local);
			if (result.kind == RichPage::BlockKind::Photo) {
				result.photoId = placeholder;
			} else {
				result.documentId = placeholder;
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
				const auto local = LocalMediaPath(
					child.media.source,
					context.basePath);
				if (local.isEmpty()) {
					continue;
				}
				const auto placeholder = ImportedMediaPlaceholderId(
					int(context.localMediaPaths.size()));
				context.localMediaPaths.push_back(local);
				if (media.kind == RichPage::BlockKind::Photo) {
					media.photoId = placeholder;
				} else {
					media.documentId = placeholder;
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
	if (blocks.empty() && context.localMediaPaths.isEmpty()) {
		return std::nullopt;
	}
	return BlocksImportResult{
		.blocks = std::move(blocks),
		.localMediaPaths = std::move(context.localMediaPaths),
		.truncated = parsed->truncated,
	};
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

} // namespace Iv::Editor

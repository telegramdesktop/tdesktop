/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_clipboard.h"

#include "base/random.h"

#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtCore/QStringList>

#include <array>

namespace Iv::Editor {
namespace {

struct ClipboardStorage {
	uint64 sessionId = base::RandomValue<uint64>();
	uint64 serial = 0;
	QPointer<QMimeData> mimeData;
	std::optional<ClipboardData> data;
};

[[nodiscard]] ClipboardStorage &Storage() {
	static auto storage = ClipboardStorage();
	return storage;
}

[[nodiscard]] bool MarkerMatches(const QMimeData *mimeData) {
	return mimeData
		&& mimeData->hasFormat(ClipboardMimeType())
		&& (mimeData->data(ClipboardMimeType()) == "1");
}

[[nodiscard]] bool OriginMatches(
		const ClipboardOrigin &origin,
		const ClipboardStorage &storage) {
	return (origin.sessionId == storage.sessionId)
		&& (origin.serial == storage.serial)
		&& (origin.serial != 0);
}

[[nodiscard]] bool StoredDataMatches(
		const ClipboardData &data,
		const ClipboardStorage &storage) {
	return std::visit([&](const auto &payload) {
		return OriginMatches(payload.origin, storage);
	}, data);
}

[[nodiscard]] ClipboardData StampClipboardData(ClipboardData data) {
	auto &storage = Storage();
	const auto serial = ++storage.serial;
	return std::visit([&](auto payload) -> ClipboardData {
		payload.origin.sessionId = storage.sessionId;
		payload.origin.serial = serial;
		return ClipboardData(std::move(payload));
	}, std::move(data));
}

[[nodiscard]] QString QtWindowsMimeName(const QString &name) {
	return u"application/x-qt-windows-mime;value=\"%1\""_q.arg(name);
}

[[nodiscard]] bool HasNativeFormat(
		const QMimeData *mimeData,
		const QString &name) {
	return mimeData->hasFormat(name)
		|| mimeData->hasFormat(QtWindowsMimeName(name));
}

[[nodiscard]] bool HasExcelNativeFormat(const QMimeData *mimeData) {
	const auto names = std::array{
		u"Biff12"_q,
		u"Biff8"_q,
		u"Biff5"_q,
		u"XML Spreadsheet"_q,
	};
	for (const auto &name : names) {
		if (HasNativeFormat(mimeData, name)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool IsExcelHtml(const QString &html) {
	const auto folded = html.toCaseFolded();
	if (!folded.contains(u"<table"_q)) {
		return false;
	}
	const auto excelGenerator = folded.contains(u"generator"_q)
		&& folded.contains(u"microsoft excel"_q);
	const auto excelProgId = folded.contains(u"progid"_q)
		&& folded.contains(u"excel.sheet"_q);
	const auto excelNamespace = folded.contains(
		u"urn:schemas-microsoft-com:office:excel"_q);
	return excelGenerator || excelProgId || excelNamespace;
}

[[nodiscard]] bool HasExcelHtml(const QMimeData *mimeData) {
	if (mimeData->hasHtml() && IsExcelHtml(mimeData->html())) {
		return true;
	}
	const auto name = u"HTML Format"_q;
	const auto wrapped = QtWindowsMimeName(name);
	return (mimeData->hasFormat(name)
			&& IsExcelHtml(QString::fromUtf8(mimeData->data(name))))
		|| (mimeData->hasFormat(wrapped)
			&& IsExcelHtml(QString::fromUtf8(mimeData->data(wrapped))));
}

[[nodiscard]] std::vector<QString> SplitRows(const QString &text) {
	auto result = std::vector<QString>();
	auto start = 0;
	for (auto i = 0; i != text.size(); ++i) {
		if (text[i] != u'\r' && text[i] != u'\n') {
			continue;
		}
		result.push_back(text.mid(start, i - start));
		if (text[i] == u'\r'
			&& i + 1 != text.size()
			&& text[i + 1] == u'\n') {
			++i;
		}
		start = i + 1;
	}
	result.push_back(text.mid(start));
	if (text.endsWith(u'\r') || text.endsWith(u'\n')) {
		result.pop_back();
	}
	return result;
}

} // namespace

QString ClipboardMimeType() {
	return u"application/x-td-iv-editor"_q;
}

std::unique_ptr<QMimeData> MimeDataFromClipboardData(ClipboardData data) {
	auto &storage = Storage();
	storage.data = StampClipboardData(std::move(data));
	auto result = std::make_unique<QMimeData>();
	result->setData(ClipboardMimeType(), "1");
	storage.mimeData = result.get();
	return result;
}

std::optional<ClipboardData> ClipboardDataFromMimeData(
		const QMimeData *mimeData) {
	const auto &storage = Storage();
	if (!MarkerMatches(mimeData)
		|| !storage.data
		|| !storage.mimeData
		|| (storage.mimeData.data() != mimeData)
		|| !StoredDataMatches(*storage.data, storage)) {
		return std::nullopt;
	}
	return storage.data;
}

std::optional<RichPage::Block> ExcelTableBlockFromMimeData(
		const QMimeData *mimeData) {
	if (!mimeData
		|| !mimeData->hasText()
		|| (!HasExcelNativeFormat(mimeData) && !HasExcelHtml(mimeData))) {
		return std::nullopt;
	}
	const auto text = mimeData->text();
	if (text.isEmpty()) {
		return std::nullopt;
	}
	const auto rows = SplitRows(text);
	if (rows.empty()) {
		return std::nullopt;
	}
	auto cells = std::vector<QStringList>();
	cells.reserve(rows.size());
	for (const auto &row : rows) {
		cells.push_back(row.split(u'\t', Qt::KeepEmptyParts));
	}
	const auto columns = cells.front().size();
	for (const auto &row : cells) {
		if (row.size() != columns) {
			return std::nullopt;
		}
	}

	auto result = RichPage::Block();
	result.kind = RichPage::BlockKind::Table;
	result.bordered = true;
	result.tableRows.reserve(cells.size());
	for (const auto &fields : cells) {
		auto row = RichPage::TableRow();
		row.cells.reserve(fields.size());
		for (const auto &field : fields) {
			auto cell = RichPage::TableCell();
			cell.text.text.text = field;
			row.cells.push_back(std::move(cell));
		}
		result.tableRows.push_back(std::move(row));
	}
	return result;
}

} // namespace Iv::Editor

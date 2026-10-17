/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/statistics_xlsx.h"

#include "base/zlib_help.h"

namespace Statistic::Xlsx {
namespace {

constexpr auto kEpochSerial = 25569.;
constexpr auto kMillisecondsInDay = 86400000.;

constexpr auto kStyleGeneral = 0;
constexpr auto kStyleDate = 1;
constexpr auto kStyleDateTime = 2;
constexpr auto kStyleHeader = 3;
constexpr auto kStyleHeaderRight = 4;

constexpr auto kMaxSheetNameLength = 31;

constexpr auto kColumnPadding = 2;
constexpr auto kHeaderPadding = 3;
constexpr auto kMinColumnWidth = 8;
constexpr auto kMaxColumnWidth = 48;

constexpr auto kDateWidth = 10;
constexpr auto kDateTimeWidth = 16;

constexpr auto kValueDigits = 6;
constexpr auto kSerialDigits = 10;

[[nodiscard]] QString ColumnName(int index) {
	Expects(index >= 0);

	auto result = QString();
	for (auto i = index + 1; i > 0; i = (i - 1) / 26) {
		result.prepend(QChar('A' + ((i - 1) % 26)));
	}
	return result;
}

[[nodiscard]] QString Escaped(const QString &text) {
	auto result = QString();
	result.reserve(text.size());
	for (const auto &ch : text) {
		const auto code = ch.unicode();
		if (ch == '&') {
			result.append(u"&amp;"_q);
		} else if (ch == '<') {
			result.append(u"&lt;"_q);
		} else if (ch == '>') {
			result.append(u"&gt;"_q);
		} else if (ch == '"') {
			result.append(u"&quot;"_q);
		} else if (ch == '\'') {
			result.append(u"&apos;"_q);
		} else if (code >= ' '
			|| code == '\t'
			|| code == '\n'
			|| code == '\r') {
			result.append(ch);
		}
	}
	return result;
}

[[nodiscard]] QString NumberText(float64 value, int digits) {
	const auto rounded = base::SafeRound(value);
	return ((value == rounded) && (std::abs(value) < 1e15))
		? QString::number(int64(rounded))
		: QString::number(value, 'f', digits);
}

[[nodiscard]] QString SheetName(
		const QString &name,
		int index,
		int limit = kMaxSheetNameLength) {
	const auto forbidden = u":\\/?*[]"_q;
	auto result = QString();
	for (const auto &ch : name) {
		if (!forbidden.contains(ch)) {
			result.append(ch);
		}
	}
	result = result.trimmed();
	if (result.size() > limit) {
		result = result.mid(0, limit).trimmed();
	}
	return result.isEmpty() ? u"Sheet%1"_q.arg(index + 1) : result;
}

[[nodiscard]] std::vector<QString> UniqueSheetNames(
		const std::vector<Sheet> &sheets) {
	auto result = std::vector<QString>();
	result.reserve(sheets.size());
	for (auto i = 0; i != int(sheets.size()); ++i) {
		auto name = SheetName(sheets[i].name, i);
		for (auto attempt = 2; ranges::contains(result, name); ++attempt) {
			const auto suffix = u" (%1)"_q.arg(attempt);
			name = SheetName(
				sheets[i].name,
				i,
				kMaxSheetNameLength - suffix.size()) + suffix;
		}
		result.push_back(name);
	}
	return result;
}

[[nodiscard]] std::vector<bool> NumericColumns(const Sheet &sheet) {
	auto result = std::vector<bool>();
	auto filled = std::vector<bool>();
	for (auto i = 1; i != int(sheet.rows.size()); ++i) {
		const auto &row = sheet.rows[i];
		if (result.size() < row.size()) {
			result.resize(row.size(), true);
			filled.resize(row.size(), false);
		}
		for (auto j = 0; j != int(row.size()); ++j) {
			switch (row[j].type) {
			case Cell::Type::Empty: break;
			case Cell::Type::Number:
			case Cell::Type::Date:
			case Cell::Type::DateTime: filled[j] = true; break;
			default: result[j] = false; break;
			}
		}
	}
	for (auto i = 0; i != int(result.size()); ++i) {
		result[i] = result[i] && filled[i];
	}
	return result;
}

[[nodiscard]] QByteArray CellContent(
		const Cell &cell,
		const QString &ref,
		bool numericColumn) {
	const auto open = [&](int style, const QString &type) {
		auto result = u"<c r=\"%1\""_q.arg(ref);
		if (style != kStyleGeneral) {
			result.append(u" s=\"%1\""_q.arg(style));
		}
		if (!type.isEmpty()) {
			result.append(u" t=\"%1\""_q.arg(type));
		}
		return result.append('>');
	};
	const auto text = [&](int style) {
		return open(style, u"inlineStr"_q)
			+ u"<is><t xml:space=\"preserve\">"_q
			+ Escaped(cell.text)
			+ u"</t></is></c>"_q;
	};
	const auto number = [&](int style, int digits) {
		return open(style, QString())
			+ u"<v>"_q
			+ NumberText(cell.number, digits)
			+ u"</v></c>"_q;
	};
	switch (cell.type) {
	case Cell::Type::Text: return text(kStyleGeneral).toUtf8();
	case Cell::Type::Header:
		return text(numericColumn ? kStyleHeaderRight : kStyleHeader).toUtf8();
	case Cell::Type::Number:
		return number(kStyleGeneral, kValueDigits).toUtf8();
	case Cell::Type::Date:
		return number(kStyleDate, kSerialDigits).toUtf8();
	case Cell::Type::DateTime:
		return number(kStyleDateTime, kSerialDigits).toUtf8();
	}
	return QByteArray();
}

[[nodiscard]] int CellWidth(const Cell &cell) {
	switch (cell.type) {
	case Cell::Type::Empty: return 0;
	case Cell::Type::Text: return cell.text.size() + kColumnPadding;
	case Cell::Type::Header: return cell.text.size() + kHeaderPadding;
	case Cell::Type::Number:
		return NumberText(cell.number, kValueDigits).size() + kColumnPadding;
	case Cell::Type::Date: return kDateWidth + kColumnPadding;
	case Cell::Type::DateTime: return kDateTimeWidth + kColumnPadding;
	}
	return 0;
}

[[nodiscard]] QByteArray ColumnsContent(const Sheet &sheet) {
	auto widths = std::vector<int>();
	for (const auto &row : sheet.rows) {
		if (widths.size() < row.size()) {
			widths.resize(row.size(), 0);
		}
		for (auto i = 0; i != int(row.size()); ++i) {
			widths[i] = std::max(widths[i], CellWidth(row[i]));
		}
	}
	if (widths.empty()) {
		return QByteArray();
	}
	auto result = QByteArray("<cols>");
	for (auto i = 0; i != int(widths.size()); ++i) {
		const auto width = std::clamp(
			widths[i],
			kMinColumnWidth,
			kMaxColumnWidth);
		result.append(u"<col min=\"%1\" max=\"%1\" width=\"%2\""
			" customWidth=\"1\"/>"_q.arg(i + 1).arg(width).toUtf8());
	}
	return result.append("</cols>");
}

[[nodiscard]] bool StartsWithHeader(const Sheet &sheet) {
	return !sheet.rows.empty()
		&& ranges::any_of(sheet.rows.front(), [](const Cell &cell) {
			return cell.type == Cell::Type::Header;
		});
}

[[nodiscard]] QByteArray SheetContent(const Sheet &sheet) {
	const auto numeric = NumericColumns(sheet);
	auto result = QByteArray();
	result.append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
		"<worksheet xmlns=\""
		"http://schemas.openxmlformats.org/spreadsheetml/2006/main"
		"\">");
	if (StartsWithHeader(sheet)) {
		result.append("<sheetViews><sheetView workbookViewId=\"0\">"
			"<pane ySplit=\"1\" topLeftCell=\"A2\""
			" activePane=\"bottomLeft\" state=\"frozen\"/>"
			"</sheetView></sheetViews>");
	}
	result.append(ColumnsContent(sheet));
	result.append("<sheetData>");
	for (auto i = 0; i != int(sheet.rows.size()); ++i) {
		const auto &row = sheet.rows[i];
		const auto number = i + 1;
		result.append(u"<row r=\"%1\">"_q.arg(number).toUtf8());
		for (auto j = 0; j != int(row.size()); ++j) {
			if (row[j].type != Cell::Type::Empty) {
				result.append(CellContent(
					row[j],
					ColumnName(j) + QString::number(number),
					(j < int(numeric.size())) && numeric[j]));
			}
		}
		result.append("</row>");
	}
	result.append("</sheetData></worksheet>");
	return result;
}

[[nodiscard]] QByteArray StylesContent() {
	return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
		"<styleSheet xmlns=\""
		"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
		"<fonts count=\"2\">"
		"<font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
		"<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font>"
		"</fonts>"
		"<fills count=\"2\">"
		"<fill><patternFill patternType=\"none\"/></fill>"
		"<fill><patternFill patternType=\"gray125\"/></fill>"
		"</fills>"
		"<borders count=\"1\"><border/></borders>"
		"<cellStyleXfs count=\"1\">"
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>"
		"</cellStyleXfs>"
		"<cellXfs count=\"5\">"
		"<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
		"<xf numFmtId=\"14\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\""
		" applyNumberFormat=\"1\"/>"
		"<xf numFmtId=\"22\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\""
		" applyNumberFormat=\"1\"/>"
		"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\""
		" applyFont=\"1\"/>"
		"<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\""
		" applyFont=\"1\" applyAlignment=\"1\">"
		"<alignment horizontal=\"right\"/>"
		"</xf>"
		"</cellXfs>"
		"</styleSheet>";
}

[[nodiscard]] QByteArray WorkbookContent(const std::vector<QString> &names) {
	auto result = QByteArray();
	result.append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
		"<workbook xmlns=\""
		"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\""
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships"
		"\"><sheets>");
	for (auto i = 0; i != int(names.size()); ++i) {
		result.append(u"<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>"_q
			.arg(Escaped(names[i]))
			.arg(i + 1)
			.toUtf8());
	}
	result.append("</sheets></workbook>");
	return result;
}

[[nodiscard]] QByteArray WorkbookRelationsContent(int count) {
	auto result = QByteArray();
	result.append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
		"<Relationships xmlns=\""
		"http://schemas.openxmlformats.org/package/2006/relationships\">");
	const auto add = [&](int id, const QString &type, const QString &target) {
		result.append(u"<Relationship Id=\"rId%1\" Type=\""
			"http://schemas.openxmlformats.org/officeDocument/2006/"
			"relationships/%2\" Target=\"%3\"/>"_q
			.arg(id)
			.arg(type)
			.arg(target)
			.toUtf8());
	};
	for (auto i = 0; i != count; ++i) {
		add(i + 1, u"worksheet"_q, u"worksheets/sheet%1.xml"_q.arg(i + 1));
	}
	add(count + 1, u"styles"_q, u"styles.xml"_q);
	result.append("</Relationships>");
	return result;
}

[[nodiscard]] QByteArray RootRelationsContent() {
	return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
		"<Relationships xmlns=\""
		"http://schemas.openxmlformats.org/package/2006/relationships\">"
		"<Relationship Id=\"rId1\" Type=\""
		"http://schemas.openxmlformats.org/officeDocument/2006/relationships"
		"/officeDocument\" Target=\"xl/workbook.xml\"/>"
		"</Relationships>";
}

[[nodiscard]] QByteArray ContentTypesContent(int count) {
	auto result = QByteArray();
	result.append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
		"<Types xmlns=\""
		"http://schemas.openxmlformats.org/package/2006/content-types\">"
		"<Default Extension=\"rels\" ContentType=\""
		"application/vnd.openxmlformats-package.relationships+xml\"/>"
		"<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
		"<Override PartName=\"/xl/workbook.xml\" ContentType=\""
		"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet."
		"main+xml\"/>"
		"<Override PartName=\"/xl/styles.xml\" ContentType=\""
		"application/vnd.openxmlformats-officedocument.spreadsheetml"
		".styles+xml\"/>");
	for (auto i = 0; i != count; ++i) {
		result.append(u"<Override PartName=\"/xl/worksheets/sheet%1.xml\""
			" ContentType=\"application/vnd.openxmlformats-officedocument"
			".spreadsheetml.worksheet+xml\"/>"_q.arg(i + 1).toUtf8());
	}
	result.append("</Types>");
	return result;
}

} // namespace

Cell Text(const QString &text) {
	return { .type = Cell::Type::Text, .text = text };
}

Cell Header(const QString &text) {
	return { .type = Cell::Type::Header, .text = text };
}

Cell Number(float64 value) {
	return { .type = Cell::Type::Number, .number = value };
}

Cell Date(float64 milliseconds) {
	return {
		.type = Cell::Type::Date,
		.number = (milliseconds / kMillisecondsInDay) + kEpochSerial,
	};
}

Cell DateTime(float64 milliseconds) {
	return {
		.type = Cell::Type::DateTime,
		.number = (milliseconds / kMillisecondsInDay) + kEpochSerial,
	};
}

QByteArray Serialize(const std::vector<Sheet> &sheets) {
	if (sheets.empty()) {
		return QByteArray();
	}
	const auto names = UniqueSheetNames(sheets);
	const auto count = int(sheets.size());

	auto zip = zlib::FileToWrite();
	auto info = zip_fileinfo{ { 0, 0, 0, 0, 0, 0 }, 0, 0, 0 };
	const auto add = [&](const QString &path, const QByteArray &content) {
		zip.openNewFile(
			path.toUtf8().constData(),
			&info,
			nullptr,
			0,
			nullptr,
			0,
			nullptr,
			Z_DEFLATED,
			Z_DEFAULT_COMPRESSION);
		zip.writeInFile(content.constData(), content.size());
		zip.closeFile();
	};

	add(u"[Content_Types].xml"_q, ContentTypesContent(count));
	add(u"_rels/.rels"_q, RootRelationsContent());
	add(u"xl/workbook.xml"_q, WorkbookContent(names));
	add(u"xl/_rels/workbook.xml.rels"_q, WorkbookRelationsContent(count));
	add(u"xl/styles.xml"_q, StylesContent());
	for (auto i = 0; i != count; ++i) {
		add(
			u"xl/worksheets/sheet%1.xml"_q.arg(i + 1),
			SheetContent(sheets[i]));
	}
	zip.close();

	return (zip.error() == ZIP_OK) ? zip.result() : QByteArray();
}

} // namespace Statistic::Xlsx

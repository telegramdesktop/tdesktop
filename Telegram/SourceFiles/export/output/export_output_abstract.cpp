/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_abstract.h"

#include "export/output/export_output_html_and_json.h"
#include "export/output/export_output_html.h"
#include "export/output/export_output_json.h"

#include <QtCore/QDir>
#include <QtCore/QDate>

namespace Export {
namespace Output {

QString NormalizePath(const Settings &settings) {
	QDir folder(settings.path);
	const auto path = folder.absolutePath();
	auto result = path.endsWith('/') ? path : (path + '/');
	if (!folder.exists() && !settings.forceSubPath) {
		return result;
	}
	const auto mode = QDir::AllEntries | QDir::NoDotAndDotDot;
	const auto list = folder.entryInfoList(mode);
	if (list.isEmpty() && !settings.forceSubPath) {
		return result;
	}
	const auto date = QDate::currentDate();
	const auto base = QString(settings.onlySinglePeer()
		? "ChatExport_%1"
		: "DataExport_%1"
	).arg(date.toString(Qt::ISODate));
	const auto add = [&](int i) {
		return base + (i ? " (" + QString::number(i) + ')' : QString());
	};
	auto index = 0;
	while (QDir(result + add(index)).exists()) {
		++index;
	}
	result += add(index) + '/';
	return result;
}

std::unique_ptr<AbstractWriter> CreateWriter(Format format) {
	switch (format) {
	case Format::Html: return std::make_unique<HtmlWriter>();
	case Format::Json: return std::make_unique<JsonWriter>();
	case Format::HtmlAndJson: return std::make_unique<HtmlAndJsonWriter>();
	}
	Unexpected("Format in Export::Output::CreateWriter.");
}

} // namespace Output
} // namespace Export

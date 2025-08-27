/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_html_and_json.h"

#include "export/output/export_output_html.h"
#include "export/output/export_output_json.h"
#include "export/output/export_output_result.h"

namespace Export::Output {

HtmlAndJsonWriter::HtmlAndJsonWriter() {
	_writers.push_back(CreateWriter(Format::Html));
	_writers.push_back(CreateWriter(Format::Json));
}

Format HtmlAndJsonWriter::format() {
	return Format::HtmlAndJson;
}

Result HtmlAndJsonWriter::start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) {
	return invoke([&](WriterPtr w) {
		return w->start(settings, environment, stats);
	});
}

Result HtmlAndJsonWriter::writePersonal(const Data::PersonalInfo &data) {
	return invoke([&](WriterPtr w) {
		return w->writePersonal(data);
	});
}

Result HtmlAndJsonWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	return invoke([&](WriterPtr w) {
		return w->writeUserpicsStart(data);
	});
}

Result HtmlAndJsonWriter::writeUserpicsSlice(const Data::UserpicsSlice &d) {
	return invoke([&](WriterPtr w) {
		return w->writeUserpicsSlice(d);
	});
}

Result HtmlAndJsonWriter::writeUserpicsEnd() {
	return invoke([&](WriterPtr w) {
		return w->writeUserpicsEnd();
	});
}

Result HtmlAndJsonWriter::writeStoriesStart(const Data::StoriesInfo &data) {
	return invoke([&](WriterPtr w) {
		return w->writeStoriesStart(data);
	});
}

Result HtmlAndJsonWriter::writeStoriesSlice(const Data::StoriesSlice &data) {
	return invoke([&](WriterPtr w) {
		return w->writeStoriesSlice(data);
	});
}

Result HtmlAndJsonWriter::writeStoriesEnd() {
	return invoke([&](WriterPtr w) {
		return w->writeStoriesEnd();
	});
}

Result HtmlAndJsonWriter::writeContactsList(const Data::ContactsList &data) {
	return invoke([&](WriterPtr w) {
		return w->writeContactsList(data);
	});
}

Result HtmlAndJsonWriter::writeSessionsList(const Data::SessionsList &data) {
	return invoke([&](WriterPtr w) {
		return w->writeSessionsList(data);
	});
}

Result HtmlAndJsonWriter::writeOtherData(const Data::File &data) {
	return invoke([&](WriterPtr w) {
		return w->writeOtherData(data);
	});
}

Result HtmlAndJsonWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	return invoke([&](WriterPtr w) {
		return w->writeDialogsStart(data);
	});
}

Result HtmlAndJsonWriter::writeDialogStart(const Data::DialogInfo &data) {
	return invoke([&](WriterPtr w) {
		return w->writeDialogStart(data);
	});
}

Result HtmlAndJsonWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	return invoke([&](WriterPtr w) {
		return w->writeDialogSlice(data);
	});
}

Result HtmlAndJsonWriter::writeDialogEnd() {
	return invoke([&](WriterPtr w) {
		return w->writeDialogEnd();
	});
}

Result HtmlAndJsonWriter::writeDialogsEnd() {
	return invoke([&](WriterPtr w) {
		return w->writeDialogsEnd();
	});
}

Result HtmlAndJsonWriter::finish() {
	return invoke([&](WriterPtr w) {
		return w->finish();
	});
}

QString HtmlAndJsonWriter::mainFilePath() {
	return _writers.front()->mainFilePath();
}

HtmlAndJsonWriter::~HtmlAndJsonWriter() = default;

Result HtmlAndJsonWriter::invoke(Fn<Result(WriterPtr)> method) const {
	auto result = Result(Result::Type::Success, QString());
	for (const auto &writer : _writers) {
		const auto current = method(writer);
		if (!current) {
			result = current;
		}
	}
	return result;
}

} // namespace Export::Output


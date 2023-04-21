/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_both.h"

#include "export/output/export_output_result.h"
#include "export/output/export_output_html.h"
#include "export/output/export_output_json.h"

namespace Export {
namespace Output {

BothWriter::BothWriter() {
	_html = std::make_shared<HtmlWriter>();
	_json = std::make_shared<JsonWriter>();
}

Result BothWriter::start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) {
	Expects(_html != nullptr);
	Expects(_html != nullptr);

	Result res = _html->start(settings, environment, stats);
	if(res.isError()) {
		return res;
	}
	return _json->start(settings, environment, stats);
}

Result BothWriter::writePersonal(const Data::PersonalInfo &data) {
	return runForBoth<Data::PersonalInfo, &AbstractWriter::writePersonal>(data);
}

Result BothWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	return runForBoth<Data::UserpicsInfo, &AbstractWriter::writeUserpicsStart>(data);
}

Result BothWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	return runForBoth<Data::UserpicsSlice, &AbstractWriter::writeUserpicsSlice>(data);
}

Result BothWriter::writeUserpicsEnd() {
	return runForBoth<&AbstractWriter::writeUserpicsEnd>();
}

Result BothWriter::writeContactsList(const Data::ContactsList &data) {
	return runForBoth<Data::ContactsList, &AbstractWriter::writeContactsList>(data);
}

Result BothWriter::writeSessionsList(const Data::SessionsList &data) {
	return runForBoth<Data::SessionsList, &AbstractWriter::writeSessionsList>(data);
}

Result BothWriter::writeOtherData(const Data::File &data) {
	return runForBoth<Data::File, &AbstractWriter::writeOtherData>(data);
}

Result BothWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	return runForBoth<Data::DialogsInfo, &AbstractWriter::writeDialogsStart>(data);
}

Result BothWriter::writeDialogStart(const Data::DialogInfo &data) {
	return runForBoth<Data::DialogInfo, &AbstractWriter::writeDialogStart>(data);
}

Result BothWriter::writeDialogSlice(const Data::MessagesSlice &data) {
	return runForBoth<Data::MessagesSlice, &AbstractWriter::writeDialogSlice>(data);
}

Result BothWriter::writeDialogEnd() {
	return runForBoth<&AbstractWriter::writeDialogEnd>();
}

Result BothWriter::writeDialogsEnd() {
	return runForBoth<&AbstractWriter::writeDialogsEnd>();
}

Result BothWriter::finish() {
	return runForBoth<&AbstractWriter::finish>();
}

QString BothWriter::mainFilePath() {
	return _html->mainFilePath();
}

template<typename ARG, Result (AbstractWriter::* FUNC)(const ARG&)>
Result BothWriter::runForBoth(const ARG& data) {
	Result res = (_html.get()->*FUNC)(data);
	if(res.isError()) {
		return res;
	}
	return (_json.get()->*FUNC)(data);
}

template<Result (AbstractWriter::* FUNC)()>
Result BothWriter::runForBoth() {
	Result res = (_html.get()->*FUNC)();
	if(res.isError()) {
		return res;
	}
	return (_json.get()->*FUNC)();
}

} // namespace Output
} // namespace Export

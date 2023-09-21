/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/output/export_output_abstract.h"

namespace Export::Output {

class HtmlWriter;
class JsonWriter;
struct Result;

class HtmlAndJsonWriter final : public AbstractWriter {
public:
	HtmlAndJsonWriter();

	Format format() override;

	Result start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) override;

	Result writePersonal(const Data::PersonalInfo &data) override;

	Result writeUserpicsStart(const Data::UserpicsInfo &data) override;
	Result writeUserpicsSlice(const Data::UserpicsSlice &data) override;
	Result writeUserpicsEnd() override;

	Result writeStoriesStart(const Data::StoriesInfo &data) override;
	Result writeStoriesSlice(const Data::StoriesSlice &data) override;
	Result writeStoriesEnd() override;

	Result writeContactsList(const Data::ContactsList &data) override;

	Result writeSessionsList(const Data::SessionsList &data) override;

	Result writeOtherData(const Data::File &data) override;

	Result writeDialogsStart(const Data::DialogsInfo &data) override;
	Result writeDialogStart(const Data::DialogInfo &data) override;
	Result writeDialogSlice(const Data::MessagesSlice &data) override;
	Result writeDialogEnd() override;
	Result writeDialogsEnd() override;

	Result finish() override;

	QString mainFilePath() override;

	~HtmlAndJsonWriter();

private:
	using WriterPtr = const std::unique_ptr<AbstractWriter> &;
	Result invoke(Fn<Result(WriterPtr)> method) const;

	std::vector<std::unique_ptr<AbstractWriter>> _writers;

};

} // namespace Export::Output

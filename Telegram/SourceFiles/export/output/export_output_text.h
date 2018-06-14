/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/output/export_output_abstract.h"
#include "export/output/export_output_file.h"
#include "export/export_settings.h"

namespace Export {
namespace Output {

class TextWriter : public AbstractWriter {
public:
	bool start(const Settings &settings) override;

	bool writePersonal(const Data::PersonalInfo &data) override;

	bool writeUserpicsStart(const Data::UserpicsInfo &data) override;
	bool writeUserpicsSlice(const Data::UserpicsSlice &data) override;
	bool writeUserpicsEnd() override;

	bool writeContactsList(const Data::ContactsList &data) override;

	bool writeSessionsList(const Data::SessionsList &data) override;

	bool writeDialogsStart(const Data::DialogsInfo &data) override;
	bool writeDialogStart(const Data::DialogInfo &data) override;
	bool writeMessagesSlice(const Data::MessagesSlice &data) override;
	bool writeDialogEnd() override;
	bool writeDialogsEnd() override;

	bool finish() override;

	QString mainFilePath() override;

private:
	QString mainFileRelativePath() const;
	QString pathWithRelativePath(const QString &path) const;
	std::unique_ptr<File> fileWithRelativePath(const QString &path) const;

	Settings _settings;

	std::unique_ptr<File> _result;
	int _userpicsCount = 0;

	int _dialogsCount = 0;
	int _dialogIndex = 0;
	std::unique_ptr<File> _dialog;

};

} // namespace Output
} // namespace Export

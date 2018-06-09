/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/output/export_output_abstract.h"
#include "export/output/export_output_file.h"

namespace Export {
namespace Output {

class TextWriter : public AbstractWriter {
public:
	bool start(const QString &folder) override;

	bool writePersonal(const Data::PersonalInfo &data) override;

	bool writeUserpicsStart(const Data::UserpicsInfo &data) override;
	bool writeUserpicsSlice(const Data::UserpicsSlice &data) override;
	bool writeUserpicsEnd() override;

	bool writeContactsList(const Data::ContactsList &data) override;

	bool writeSessionsList(const Data::SessionsList &data) override;

	bool writeChatsStart(const Data::ChatsInfo &data) override;
	bool writeChatStart(const Data::ChatInfo &data) override;
	bool writeMessagesSlice(const Data::MessagesSlice &data) override;
	bool writeChatEnd() override;
	bool writeChatsEnd() override;

	bool finish() override;

	QString mainFilePath() override;

private:
	QString _folder;

	std::unique_ptr<File> _result;
	int _userpicsCount = 0;

};

} // namespace Output
} // namespace Export

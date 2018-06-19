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
	Result start(const Settings &settings) override;

	Result writePersonal(const Data::PersonalInfo &data) override;

	Result writeUserpicsStart(const Data::UserpicsInfo &data) override;
	Result writeUserpicsSlice(const Data::UserpicsSlice &data) override;
	Result writeUserpicsEnd() override;

	Result writeContactsList(const Data::ContactsList &data) override;

	Result writeSessionsList(const Data::SessionsList &data) override;

	Result writeDialogsStart(const Data::DialogsInfo &data) override;
	Result writeDialogStart(const Data::DialogInfo &data) override;
	Result writeDialogSlice(const Data::MessagesSlice &data) override;
	Result writeDialogEnd() override;
	Result writeDialogsEnd() override;

	Result writeLeftChannelsStart(const Data::DialogsInfo &data) override;
	Result writeLeftChannelStart(const Data::DialogInfo &data) override;
	Result writeLeftChannelSlice(const Data::MessagesSlice &data) override;
	Result writeLeftChannelEnd() override;
	Result writeLeftChannelsEnd() override;

	Result finish() override;

	QString mainFilePath() override;

private:
	QString mainFileRelativePath() const;
	QString pathWithRelativePath(const QString &path) const;
	std::unique_ptr<File> fileWithRelativePath(const QString &path) const;

	Result writeSavedContacts(const Data::ContactsList &data);
	Result writeFrequentContacts(const Data::ContactsList &data);

	Result writeChatsStart(
		const Data::DialogsInfo &data,
		const QByteArray &listName,
		const QString &fileName);
	Result writeChatStart(const Data::DialogInfo &data);
	Result writeChatSlice(const Data::MessagesSlice &data);
	Result writeChatEnd();

	Settings _settings;

	std::unique_ptr<File> _summary;
	int _userpicsCount = 0;

	int _dialogsCount = 0;
	int _dialogIndex = 0;
	bool _dialogOnlyMy = false;
	bool _dialogEmpty = true;

	std::unique_ptr<File> _chat;

};

} // namespace Output
} // namespace Export

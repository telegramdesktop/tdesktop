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
#include "export/data/export_data_types.h"

namespace Export {
namespace Output {

class HtmlWriter : public AbstractWriter {
public:
	HtmlWriter();

	Format format() override {
		return Format::Html;
	}

	Result start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) override;

	Result writePersonal(const Data::PersonalInfo &data) override;

	Result writeUserpicsStart(const Data::UserpicsInfo &data) override;
	Result writeUserpicsSlice(const Data::UserpicsSlice &data) override;
	Result writeUserpicsEnd() override;

	Result writeContactsList(const Data::ContactsList &data) override;

	Result writeSessionsList(const Data::SessionsList &data) override;

	Result writeOtherData(const Data::File &data) override;

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

	~HtmlWriter();

private:
	class Wrap;

	Result copyFile(
		const QString &source,
		const QString &relativePath) const;

	QString mainFileRelativePath() const;
	QString pathWithRelativePath(const QString &path) const;
	std::unique_ptr<Wrap> fileWithRelativePath(const QString &path) const;
	QString messagesFile(int index) const;

	Result writeSavedContacts(const Data::ContactsList &data);
	Result writeFrequentContacts(const Data::ContactsList &data);

	Result writeSessions(const Data::SessionsList &data);
	Result writeWebSessions(const Data::SessionsList &data);

	Result writeChatsStart(
		const Data::DialogsInfo &data,
		const QByteArray &listName,
		const QByteArray &about,
		const QString &fileName);
	Result writeChatStart(const Data::DialogInfo &data);
	Result writeChatSlice(const Data::MessagesSlice &data);
	Result writeChatEnd();
	Result writeChatsEnd();
	Result switchToNextChatFile(int index);

	Settings _settings;
	Environment _environment;
	Stats *_stats = nullptr;

	std::unique_ptr<Wrap> _summary;

	int _userpicsCount = 0;
	std::unique_ptr<Wrap> _userpics;

	int _dialogsCount = 0;
	int _dialogIndex = 0;
	Data::DialogInfo _dialog;

	int _messagesCount = 0;
	std::unique_ptr<Wrap> _chats;
	std::unique_ptr<Wrap> _chat;

};

} // namespace Output
} // namespace Export

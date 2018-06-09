/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Export {
namespace Data {
struct PersonalInfo;
struct UserpicsInfo;
struct UserpicsSlice;
struct ContactsList;
struct SessionsList;
struct ChatsInfo;
struct ChatInfo;
struct MessagesSlice;
} // namespace Data

namespace Output {

enum class Format {
	Text,
	Html,
	Json,
};

class AbstractWriter {
public:
	virtual bool start(const QString &folder) = 0;

	virtual bool writePersonal(const Data::PersonalInfo &data) = 0;

	virtual bool writeUserpicsStart(const Data::UserpicsInfo &data) = 0;
	virtual bool writeUserpicsSlice(const Data::UserpicsSlice &data) = 0;
	virtual bool writeUserpicsEnd() = 0;

	virtual bool writeContactsList(const Data::ContactsList &data) = 0;

	virtual bool writeSessionsList(const Data::SessionsList &data) = 0;

	virtual bool writeChatsStart(const Data::ChatsInfo &data) = 0;
	virtual bool writeChatStart(const Data::ChatInfo &data) = 0;
	virtual bool writeMessagesSlice(const Data::MessagesSlice &data) = 0;
	virtual bool writeChatEnd() = 0;
	virtual bool writeChatsEnd() = 0;

	virtual bool finish() = 0;

	virtual QString mainFilePath() = 0;

	virtual ~AbstractWriter() = default;

};

std::unique_ptr<AbstractWriter> CreateWriter(Format format);

} // namespace Output
} // namespace Export

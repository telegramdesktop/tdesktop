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
struct DialogsInfo;
struct DialogInfo;
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

	virtual bool writeDialogsStart(const Data::DialogsInfo &data) = 0;
	virtual bool writeDialogStart(const Data::DialogInfo &data) = 0;
	virtual bool writeMessagesSlice(const Data::MessagesSlice &data) = 0;
	virtual bool writeDialogEnd() = 0;
	virtual bool writeDialogsEnd() = 0;

	virtual bool finish() = 0;

	virtual QString mainFilePath() = 0;

	virtual ~AbstractWriter() = default;

};

std::unique_ptr<AbstractWriter> CreateWriter(Format format);

} // namespace Output
} // namespace Export

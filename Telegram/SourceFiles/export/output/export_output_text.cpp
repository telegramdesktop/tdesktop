/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/output/export_output_text.h"

#include "export/data/export_data_types.h"

#include <QtCore/QFile>

namespace Export {
namespace Output {
namespace {

#ifdef Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\r\n");
#else // Q_OS_WIN
const auto kLineBreak = QByteArrayLiteral("\n");
#endif // Q_OS_WIN

void SerializeMultiline(
		QByteArray &appendTo,
		const QByteArray &value,
		int newline) {
	const auto data = value.data();
	auto offset = 0;
	do {
		appendTo.append("> ");
		appendTo.append(data + offset, newline).append(kLineBreak);
		offset = newline + 1;
		newline = value.indexOf('\n', offset);
	} while (newline > 0);
}

QByteArray SerializeKeyValue(
		std::vector<std::pair<QByteArray, QByteArray>> &&values) {
	auto result = QByteArray();
	for (const auto &[key, value] : values) {
		if (value.isEmpty()) {
			continue;
		}
		result.append(key);
		if (const auto newline = value.indexOf('\n'); newline >= 0) {
			result.append(':').append(kLineBreak);
			SerializeMultiline(result, value, newline);
		} else {
			result.append(": ").append(value).append(kLineBreak);
		}
	}
	return result;
}

Data::Utf8String FormatUsername(const Data::Utf8String &username) {
	return username.isEmpty() ? username : ('@' + username);
}

} // namespace

bool TextWriter::start(const QString &folder) {
	Expects(folder.endsWith('/'));

	_folder = folder;
	_result = std::make_unique<File>(_folder + "result.txt");
	return true;
}

bool TextWriter::writePersonal(const Data::PersonalInfo &data) {
	Expects(_result != nullptr);

	const auto serialized = "Personal information"
		+ kLineBreak
		+ kLineBreak
		+ SerializeKeyValue({
		{ "First name", data.firstName },
		{ "Last name", data.lastName },
		{ "Phone number", Data::FormatPhoneNumber(data.phoneNumber) },
		{ "Username", FormatUsername(data.username) },
		{ "Bio", data.bio },
		})
		+ kLineBreak;
	return _result->writeBlock(serialized) == File::Result::Success;
}

bool TextWriter::writeUserpicsStart(const Data::UserpicsInfo &data) {
	Expects(_result != nullptr);

	_userpicsCount = data.count;
	if (!_userpicsCount) {
		return true;
	}
	const auto serialized = "Personal photos "
		"(" + Data::NumberToString(_userpicsCount) + ")"
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(serialized) == File::Result::Success;
}

bool TextWriter::writeUserpicsSlice(const Data::UserpicsSlice &data) {
	auto lines = QByteArray();
	for (const auto &userpic : data.list) {
		lines.append(userpic.date.toString().toUtf8()).append(": ");
		lines.append(userpic.image.relativePath.toUtf8());
		lines.append(kLineBreak);
	}
	return _result->writeBlock(lines) == File::Result::Success;
}

bool TextWriter::writeUserpicsEnd() {
	return (_userpicsCount > 0)
		? _result->writeBlock(kLineBreak) == File::Result::Success
		: true;
}

bool TextWriter::writeContactsList(const Data::ContactsList &data) {
	return true;
}

bool TextWriter::writeSessionsList(const Data::SessionsList &data) {
	return true;
}

bool TextWriter::writeChatsStart(const Data::ChatsInfo &data) {
	return true;
}

bool TextWriter::writeChatStart(const Data::ChatInfo &data) {
	return true;
}

bool TextWriter::writeMessagesSlice(const Data::MessagesSlice &data) {
	return true;
}

bool TextWriter::writeChatEnd() {
	return true;
}

bool TextWriter::writeChatsEnd() {
	return true;
}

bool TextWriter::finish() {
	return true;
}

QString TextWriter::mainFilePath() {
	return _folder + "result.txt";
}

} // namespace Output
} // namespace Export

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

QByteArray JoinList(
		const QByteArray &separator,
		const std::vector<QByteArray> &list) {
	if (list.empty()) {
		return QByteArray();
	} else if (list.size() == 1) {
		return list[0];
	}
	auto size = (list.size() - 1) * separator.size();
	for (const auto &value : list) {
		size += value.size();
	}
	auto result = QByteArray();
	result.reserve(size);
	auto counter = 0;
	while (true) {
		result.append(list[counter]);
		if (++counter == list.size()) {
			break;
		} else {
			result.append(separator);
		}
	}
	return result;
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
		{ "First name", data.user.firstName },
		{ "Last name", data.user.lastName },
		{ "Phone number", Data::FormatPhoneNumber(data.user.phoneNumber) },
		{ "Username", FormatUsername(data.user.username) },
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
		if (!userpic.date) {
			lines.append("(empty photo)");
		} else {
			lines.append(Data::FormatDateTime(userpic.date)).append(" - ");
			if (userpic.image.relativePath.isEmpty()) {
				lines.append("(file unavailable)");
			} else {
				lines.append(userpic.image.relativePath.toUtf8());
			}
		}
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
	if (data.list.empty()) {
		return true;
	}

	const auto file = std::make_unique<File>(_folder + "contacts.txt");
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &index : Data::SortedContactsIndices(data)) {
		const auto &contact = data.list[index];
		if (!contact.id) {
			list.push_back("(user unavailable)");
		} else if (contact.firstName.isEmpty()
			&& contact.lastName.isEmpty()
			&& contact.phoneNumber.isEmpty()) {
			list.push_back("(empty user)" + kLineBreak);
		} else {
			list.push_back(SerializeKeyValue({
				{ "First name", contact.firstName },
				{ "Last name", contact.lastName },
				{
					"Phone number",
					Data::FormatPhoneNumber(contact.phoneNumber)
				},
			}));
		}
	}
	const auto full = JoinList(kLineBreak, list);
	if (file->writeBlock(full) != File::Result::Success) {
		return false;
	}

	const auto header = "Contacts "
		"(" + Data::NumberToString(data.list.size()) + ") - contacts.txt"
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(header) == File::Result::Success;
}

bool TextWriter::writeSessionsList(const Data::SessionsList &data) {
	if (data.list.empty()) {
		return true;
	}

	const auto file = std::make_unique<File>(_folder + "sessions.txt");
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &session : data.list) {
		list.push_back(SerializeKeyValue({
			{ "Last active", Data::FormatDateTime(session.lastActive) },
			{ "Last IP address", session.ip },
			{ "Last country", session.country },
			{ "Last region", session.region },
			{
				"Application name",
				(session.applicationName.isEmpty()
					? Data::Utf8String("(unknown)")
					: session.applicationName)
			},
			{ "Application version", session.applicationVersion },
			{ "Device model", session.deviceModel },
			{ "Platform", session.platform },
			{ "System version", session.systemVersion },
			{ "Created", Data::FormatDateTime(session.created) },
		}));
	}
	const auto full = JoinList(kLineBreak, list);
	if (file->writeBlock(full) != File::Result::Success) {
		return false;
	}

	const auto header = "Sessions "
		"(" + Data::NumberToString(data.list.size()) + ") - sessions.txt"
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(header) == File::Result::Success;
}

bool TextWriter::writeDialogsStart(const Data::DialogsInfo &data) {
	if (data.list.empty()) {
		return true;
	}

	using Type = Data::DialogInfo::Type;
	const auto TypeString = [](Type type) {
		switch (type) {
		case Type::Unknown: return "(unknown)";
		case Type::Personal: return "Personal Chat";
		case Type::PrivateGroup: return "Private Group";
		case Type::PublicGroup: return "Public Group";
		case Type::Channel: return "Channel";
		}
		Unexpected("Dialog type in TypeString.");
	};
	const auto digits = Data::NumberToString(data.list.size() - 1).size();
	const auto file = std::make_unique<File>(_folder + "chats.txt");
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	auto index = 0;
	for (const auto &dialog : data.list) {
		auto number = Data::NumberToString(++index);
		auto path = QByteArray("Chats/chat_");
		for (auto i = number.size(); i < digits; ++i) {
			path += '0';
		}
		path += number + ".txt";
		list.push_back(SerializeKeyValue({
			{ "Name", dialog.name },
			{ "Type", TypeString(dialog.type) },
			{ "Content", path }
		}));
	}
	const auto full = JoinList(kLineBreak, list);
	if (file->writeBlock(full) != File::Result::Success) {
		return false;
	}

	const auto header = "Chats "
		"(" + Data::NumberToString(data.list.size()) + ") - chats.txt"
		+ kLineBreak
		+ kLineBreak;
	return _result->writeBlock(header) == File::Result::Success;
}

bool TextWriter::writeDialogStart(const Data::DialogInfo &data) {
	return true;
}

bool TextWriter::writeMessagesSlice(const Data::MessagesSlice &data) {
	return true;
}

bool TextWriter::writeDialogEnd() {
	return true;
}

bool TextWriter::writeDialogsEnd() {
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

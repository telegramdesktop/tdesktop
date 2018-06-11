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
		if (!userpic.date.isValid()) {
			lines.append("(empty photo)");
		} else {
			lines.append(Data::FormatDateTime(userpic.date.toTime_t()));
			lines.append(" - ");
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

	// Get sorted by name indices.
	const auto names = ranges::view::all(
		data.list
	) | ranges::view::transform([](const Data::User &user) {
		return (QString::fromUtf8(user.firstName)
			+ ' '
			+ QString::fromUtf8(user.lastName)).toLower();
	}) | ranges::to_vector;

	auto indices = ranges::view::ints(0, int(data.list.size()))
		| ranges::to_vector;
	ranges::sort(indices, [&](int i, int j) {
		return names[i] < names[j];
	});

	const auto header = "Contacts "
		"(" + Data::NumberToString(data.list.size()) + ")"
		+ kLineBreak
		+ kLineBreak;
	auto list = std::vector<QByteArray>();
	list.reserve(data.list.size());
	for (const auto &index : indices) {
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
	const auto full = header + JoinList(kLineBreak, list) + kLineBreak;
	return _result->writeBlock(full) == File::Result::Success;
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

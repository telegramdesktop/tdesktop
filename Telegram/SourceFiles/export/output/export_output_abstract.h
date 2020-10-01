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
struct File;
} // namespace Data

struct Settings;
struct Environment;

namespace Output {

QString NormalizePath(const Settings &settings);

struct Result;
class Stats;

enum class Format {
	Html,
	Json,
};

class AbstractWriter {
public:
	[[nodiscard]] virtual Format format() = 0;

	[[nodiscard]] virtual Result start(
		const Settings &settings,
		const Environment &environment,
		Stats *stats) = 0;

	[[nodiscard]] virtual Result writePersonal(
		const Data::PersonalInfo &data) = 0;

	[[nodiscard]] virtual Result writeUserpicsStart(
		const Data::UserpicsInfo &data) = 0;
	[[nodiscard]] virtual Result writeUserpicsSlice(
		const Data::UserpicsSlice &data) = 0;
	[[nodiscard]] virtual Result writeUserpicsEnd() = 0;

	[[nodiscard]] virtual Result writeContactsList(
		const Data::ContactsList &data) = 0;

	[[nodiscard]] virtual Result writeSessionsList(
		const Data::SessionsList &data) = 0;

	[[nodiscard]] virtual Result writeOtherData(
		const Data::File &data) = 0;

	[[nodiscard]] virtual Result writeDialogsStart(
		const Data::DialogsInfo &data) = 0;
	[[nodiscard]] virtual Result writeDialogStart(
		const Data::DialogInfo &data) = 0;
	[[nodiscard]] virtual Result writeDialogSlice(
		const Data::MessagesSlice &data) = 0;
	[[nodiscard]] virtual Result writeDialogEnd() = 0;
	[[nodiscard]] virtual Result writeDialogsEnd() = 0;

	[[nodiscard]] virtual Result finish() = 0;

	[[nodiscard]] virtual QString mainFilePath() = 0;

	virtual ~AbstractWriter() = default;

	Stats produceTestExample(
		const QString &path,
		const Environment &environment);

};

std::unique_ptr<AbstractWriter> CreateWriter(Format format);

} // namespace Output
} // namespace Export

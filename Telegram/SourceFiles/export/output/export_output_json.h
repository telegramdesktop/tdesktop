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
namespace details {

struct JsonContext {
	using Type = bool;
	static constexpr auto kObject = Type(true);
	static constexpr auto kArray = Type(false);

	// Always fun to use std::vector<bool>.
	std::vector<Type> nesting;
};

} // namespace details

class JsonWriter : public AbstractWriter {
public:
	Format format() override {
		return Format::Json;
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

private:
	using Context = details::JsonContext;

	[[nodiscard]] QByteArray pushNesting(Context::Type type);
	[[nodiscard]] QByteArray prepareObjectItemStart(const QByteArray &key);
	[[nodiscard]] QByteArray prepareArrayItemStart();
	[[nodiscard]] QByteArray popNesting();

	QString mainFileRelativePath() const;
	QString pathWithRelativePath(const QString &path) const;
	std::unique_ptr<File> fileWithRelativePath(const QString &path) const;

	Result writeSavedContacts(const Data::ContactsList &data);
	Result writeFrequentContacts(const Data::ContactsList &data);

	Result writeSessions(const Data::SessionsList &data);
	Result writeWebSessions(const Data::SessionsList &data);

	Result writeChatsStart(
		const Data::DialogsInfo &data,
		const QByteArray &listName);
	Result writeChatStart(const Data::DialogInfo &data);
	Result writeChatSlice(const Data::MessagesSlice &data);
	Result writeChatEnd();
	Result writeChatsEnd();

	Settings _settings;
	Environment _environment;
	Stats *_stats = nullptr;

	Context _context;
	bool _currentNestingHadItem = false;

	std::unique_ptr<File> _output;

};

} // namespace Output
} // namespace Export

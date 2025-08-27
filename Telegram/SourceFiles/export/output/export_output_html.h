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

class HtmlContext {
public:
	[[nodiscard]] QByteArray pushTag(
		const QByteArray &tag,
		std::map<QByteArray, QByteArray> &&attributes = {});
	[[nodiscard]] QByteArray popTag();
	[[nodiscard]] QByteArray indent() const;
	[[nodiscard]] bool empty() const;

private:
	struct Tag {
		QByteArray name;
		bool block = true;
	};
	std::vector<Tag> _tags;

};

struct UserpicData;
struct StoryData;
class PeersMap;
struct MediaData;

} // namespace details

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

	Result writeStoriesStart(const Data::StoriesInfo &data) override;
	Result writeStoriesSlice(const Data::StoriesSlice &data) override;
	Result writeStoriesEnd() override;

	Result writeContactsList(const Data::ContactsList &data) override;

	Result writeSessionsList(const Data::SessionsList &data) override;

	Result writeOtherData(const Data::File &data) override;

	Result writeDialogsStart(const Data::DialogsInfo &data) override;
	Result writeDialogStart(const Data::DialogInfo &data) override;
	Result writeDialogSlice(const Data::MessagesSlice &data) override;
	Result writeDialogEnd() override;
	Result writeDialogsEnd() override;

	Result finish() override;

	QString mainFilePath() override;

	~HtmlWriter();

private:
	using Context = details::HtmlContext;
	using UserpicData = details::UserpicData;
	using MediaData = details::MediaData;
	class Wrap;
	struct MessageInfo;
	enum class DialogsMode {
		None,
		Chats,
		Left,
	};

	[[nodiscard]] Result copyFile(
		const QString &source,
		const QString &relativePath) const;

	[[nodiscard]] QString mainFileRelativePath() const;
	[[nodiscard]] QString pathWithRelativePath(const QString &path) const;
	[[nodiscard]] std::unique_ptr<Wrap> fileWithRelativePath(
		const QString &path) const;
	[[nodiscard]] QString messagesFile(int index) const;

	[[nodiscard]] Result writeSavedContacts(const Data::ContactsList &data);
	[[nodiscard]] Result writeFrequentContacts(const Data::ContactsList &data);

	[[nodiscard]] Result writeSessions(const Data::SessionsList &data);
	[[nodiscard]] Result writeWebSessions(const Data::SessionsList &data);

	[[nodiscard]] Result validateDialogsMode(bool isLeftChannel);
	[[nodiscard]] Result writeDialogOpening(int index);
	[[nodiscard]] Result switchToNextChatFile(int index);
	[[nodiscard]] Result writeEmptySinglePeer();

	void pushSection(
		int priority,
		const QByteArray &label,
		const QByteArray &type,
		int count,
		const QString &path);
	[[nodiscard]] Result writeSections();

	[[nodiscard]] Result writeDefaultPersonal(
		const Data::PersonalInfo &data);
	[[nodiscard]] Result writeDelayedPersonal(const QString &userpicPath);
	[[nodiscard]] Result writePreparedPersonal(
		const Data::PersonalInfo &data,
		const QString &userpicPath);
	void pushUserpicsSection();
	void pushStoriesSection();

	[[nodiscard]] QString userpicsFilePath() const;
	[[nodiscard]] QString storiesFilePath() const;

	[[nodiscard]] QByteArray wrapMessageLink(
		int messageId,
		QByteArray text);

	Settings _settings;
	Environment _environment;
	Stats *_stats = nullptr;

	struct SavedSection;
	std::vector<SavedSection> _savedSections;

	std::unique_ptr<Wrap> _summary;
	bool _summaryNeedDivider = false;
	bool _haveSections = false;

	uint8 _selfColorIndex = 0;
	std::unique_ptr<Data::PersonalInfo> _delayedPersonalInfo;

	int _userpicsCount = 0;
	std::unique_ptr<Wrap> _userpics;

	int _storiesCount = 0;
	std::unique_ptr<Wrap> _stories;

	QString _dialogsRelativePath;
	Data::DialogInfo _dialog;
	DialogsMode _dialogsMode = DialogsMode::None;

	int _messagesCount = 0;
	std::unique_ptr<MessageInfo> _lastMessageInfo;
	int _dateMessageId = 0;
	std::unique_ptr<Wrap> _chats;
	std::unique_ptr<Wrap> _chat;
	std::vector<int> _lastMessageIdsPerFile;
	bool _chatFileEmpty = false;

};

} // namespace Output
} // namespace Export

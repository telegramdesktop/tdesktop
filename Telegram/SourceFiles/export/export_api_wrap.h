/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_concurrent_sender.h"

namespace Export {
namespace Data {
struct File;
struct Chat;
struct FileLocation;
struct PersonalInfo;
struct UserpicsInfo;
struct UserpicsSlice;
struct ContactsList;
struct SessionsList;
struct DialogsInfo;
struct DialogInfo;
struct MessagesSlice;
struct Message;
struct FileOrigin;
} // namespace Data

namespace Output {
struct Result;
class Stats;
} // namespace Output

struct Settings;

class ApiWrap {
public:
	ApiWrap(QPointer<MTP::Instance> weak, Fn<void(FnMut<void()>)> runner);

	rpl::producer<MTP::Error> errors() const;
	rpl::producer<Output::Result> ioErrors() const;

	struct StartInfo {
		int userpicsCount = 0;
		int dialogsCount = 0;
	};
	void startExport(
		const Settings &settings,
		Output::Stats *stats,
		FnMut<void(StartInfo)> done);

	void requestDialogsList(
		Fn<bool(int count)> progress,
		FnMut<void(Data::DialogsInfo&&)> done);

	void requestPersonalInfo(FnMut<void(Data::PersonalInfo&&)> done);

	void requestOtherData(
		const QString &suggestedPath,
		FnMut<void(Data::File&&)> done);

	struct DownloadProgress {
		uint64 randomId = 0;
		QString path;
		int itemIndex = 0;
		int ready = 0;
		int total = 0;
	};
	void requestUserpics(
		FnMut<bool(Data::UserpicsInfo&&)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::UserpicsSlice&&)> slice,
		FnMut<void()> finish);

	void requestContacts(FnMut<void(Data::ContactsList&&)> done);

	void requestSessions(FnMut<void(Data::SessionsList&&)> done);

	void requestMessages(
		const Data::DialogInfo &info,
		FnMut<bool(const Data::DialogInfo &)> start,
		Fn<bool(DownloadProgress)> progress,
		Fn<bool(Data::MessagesSlice&&)> slice,
		FnMut<void()> done);

	void finishExport(FnMut<void()> done);
	void skipFile(uint64 randomId);
	void cancelExportFast();

	~ApiWrap();

private:
	class LoadedFileCache;
	struct StartProcess;
	struct ContactsProcess;
	struct UserpicsProcess;
	struct OtherDataProcess;
	struct FileProcess;
	struct FileProgress;
	struct ChatsProcess;
	struct LeftChannelsProcess;
	struct DialogsProcess;
	struct ChatProcess;

	void startMainSession(FnMut<void()> done);
	void sendNextStartRequest();
	void requestUserpicsCount();
	void requestSplitRanges();
	void requestDialogsCount();
	void requestLeftChannelsCount();
	void finishStartProcess();

	void requestTopPeersSlice();

	void handleUserpicsSlice(const MTPphotos_Photos &result);
	void loadUserpicsFiles(Data::UserpicsSlice &&slice);
	void loadNextUserpic();
	bool loadUserpicProgress(FileProgress value);
	void loadUserpicDone(const QString &relativePath);
	void finishUserpicsSlice();
	void finishUserpics();

	void otherDataDone(const QString &relativePath);

	bool useOnlyLastSplit() const;

	void requestDialogsSlice();
	void appendDialogsSlice(Data::DialogsInfo &&info);
	void finishDialogsList();
	void requestSinglePeerDialog();
	mtpRequestId requestSinglePeerMigrated(const Data::DialogInfo &info);
	void appendSinglePeerDialogs(Data::DialogsInfo &&info);

	void requestLeftChannelsIfNeeded();
	void requestLeftChannelsList(
		Fn<bool(int count)> progress,
		FnMut<void(Data::DialogsInfo&&)> done);
	void requestLeftChannelsSliceGeneric(FnMut<void()> done);
	void requestLeftChannelsSlice();
	void appendLeftChannelsSlice(Data::DialogsInfo &&info);

	void appendChatsSlice(
		ChatsProcess &process,
		std::vector<Data::DialogInfo> &to,
		std::vector<Data::DialogInfo> &&from,
		int splitIndex);

	void requestMessagesCount(int localSplitIndex);
	void checkFirstMessageDate(int localSplitIndex, int count);
	void messagesCountLoaded(int localSplitIndex, int count);
	void requestMessagesSlice();
	void requestChatMessages(
		int splitIndex,
		int offsetId,
		int addOffset,
		int limit,
		FnMut<void(MTPmessages_Messages&&)> done);
	void loadMessagesFiles(Data::MessagesSlice &&slice);
	void loadNextMessageFile();
	bool loadMessageFileProgress(FileProgress value);
	void loadMessageFileDone(const QString &relativePath);
	bool loadMessageThumbProgress(FileProgress value);
	void loadMessageThumbDone(const QString &relativePath);
	void finishMessagesSlice();
	void finishMessages();

	[[nodiscard]] Data::Message *currentFileMessage() const;
	[[nodiscard]] Data::FileOrigin currentFileMessageOrigin() const;

	bool processFileLoad(
		Data::File &file,
		const Data::FileOrigin &origin,
		Fn<bool(FileProgress)> progress,
		FnMut<void(QString)> done,
		Data::Message *message = nullptr);
	std::unique_ptr<FileProcess> prepareFileProcess(
		const Data::File &file,
		const Data::FileOrigin &origin) const;
	bool writePreloadedFile(
		Data::File &file,
		const Data::FileOrigin &origin);
	void loadFile(
		const Data::File &file,
		const Data::FileOrigin &origin,
		Fn<bool(FileProgress)> progress,
		FnMut<void(QString)> done);
	void loadFilePart();
	void filePartDone(int offset, const MTPupload_File &result);
	void filePartUnavailable();
	void filePartRefreshReference(int offset);
	void filePartExtractReference(
		int offset,
		const MTPmessages_Messages &result);

	template <typename Request>
	class RequestBuilder;

	template <typename Request>
	[[nodiscard]] auto mainRequest(Request &&request);

	template <typename Request>
	[[nodiscard]] auto splitRequest(int index, Request &&request);

	[[nodiscard]] auto fileRequest(
		const Data::FileLocation &location,
		int offset);

	void error(const MTP::Error &error);
	void error(const QString &text);
	void ioError(const Output::Result &result);

	MTP::ConcurrentSender _mtp;
	std::optional<uint64> _takeoutId;
	std::optional<int32> _selfId;
	Output::Stats *_stats = nullptr;

	std::unique_ptr<Settings> _settings;
	MTPInputUser _user = MTP_inputUserSelf();

	std::unique_ptr<StartProcess> _startProcess;
	std::unique_ptr<LoadedFileCache> _fileCache;
	std::unique_ptr<ContactsProcess> _contactsProcess;
	std::unique_ptr<UserpicsProcess> _userpicsProcess;
	std::unique_ptr<OtherDataProcess> _otherDataProcess;
	std::unique_ptr<FileProcess> _fileProcess;
	std::unique_ptr<LeftChannelsProcess> _leftChannelsProcess;
	std::unique_ptr<DialogsProcess> _dialogsProcess;
	std::unique_ptr<ChatProcess> _chatProcess;
	QVector<MTPMessageRange> _splits;

	rpl::event_stream<MTP::Error> _errors;
	rpl::event_stream<Output::Result> _ioErrors;

};

} // namespace Export

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/concurrent_sender.h"

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
} // namespace Data

struct Settings;

class ApiWrap {
public:
	ApiWrap(Fn<void(FnMut<void()>)> runner);

	rpl::producer<RPCError> errors() const;

	struct StartInfo {
		int userpicsCount = 0;
		int dialogsCount = 0;
		int leftChannelsCount = 0;
	};
	void startExport(
		const Settings &settings,
		FnMut<void(StartInfo)> done);

	void requestLeftChannelsList(FnMut<void(Data::DialogsInfo&&)> done);
	rpl::producer<int> leftChannelsLoadedCount() const;

	void requestDialogsList(FnMut<void(Data::DialogsInfo&&)> done);
	rpl::producer<int> dialogsLoadedCount() const;

	void requestPersonalInfo(FnMut<void(Data::PersonalInfo&&)> done);

	void requestUserpics(
		FnMut<void(Data::UserpicsInfo&&)> start,
		Fn<void(Data::UserpicsSlice&&)> slice,
		FnMut<void()> finish);

	void requestContacts(FnMut<void(Data::ContactsList&&)> done);

	void requestSessions(FnMut<void(Data::SessionsList&&)> done);

	void requestMessages(
		const Data::DialogInfo &info,
		Fn<void(Data::MessagesSlice&&)> slice,
		FnMut<void()> done);

	~ApiWrap();

private:
	class LoadedFileCache;
	struct StartProcess;
	struct UserpicsProcess;
	struct FileProcess;
	struct LeftChannelsProcess;
	struct DialogsProcess;
	struct ChatProcess;

	void startMainSession(FnMut<void()> done);
	void sendNextStartRequest();
	void requestUserpicsCount();
	void requestDialogsCount();
	void requestLeftChannelsCount();
	void finishStartProcess();

	void handleUserpicsSlice(const MTPphotos_Photos &result);
	void loadUserpicsFiles(Data::UserpicsSlice &&slice);
	void loadNextUserpic();
	void loadUserpicDone(const QString &relativePath);
	void finishUserpics();

	void requestDialogsSlice();
	void appendDialogsSlice(Data::DialogsInfo &&info);
	void finishDialogsList();

	void requestLeftChannelsSliceGeneric(FnMut<void()> done);
	void requestLeftChannelsSlice();
	void appendLeftChannelsSlice(Data::DialogsInfo &&info);

	void appendChatsSlice(Data::DialogsInfo &to, Data::DialogsInfo &&info);

	void requestMessagesSlice();
	void loadMessagesFiles(Data::MessagesSlice &&slice);
	void loadNextMessageFile();
	void loadMessageFileDone(const QString &relativePath);
	void finishMessagesSlice();
	void finishMessages();

	bool processFileLoad(
		Data::File &file,
		FnMut<void(QString)> done,
		Data::Message *message = nullptr);
	std::unique_ptr<FileProcess> prepareFileProcess(
		const Data::File &file) const;
	bool writePreloadedFile(Data::File &file);
	void loadFile(
		const Data::File &file,
		FnMut<void(QString)> done);
	void loadFilePart();
	void filePartDone(int offset, const MTPupload_File &result);

	template <typename Request>
	[[nodiscard]] auto mainRequest(Request &&request);

	[[nodiscard]] auto fileRequest(
		const Data::FileLocation &location,
		int offset);

	void error(RPCError &&error);
	void error(const QString &text);

	MTP::ConcurrentSender _mtp;
	base::optional<uint64> _takeoutId;

	std::unique_ptr<Settings> _settings;
	MTPInputUser _user = MTP_inputUserSelf();

	std::unique_ptr<StartProcess> _startProcess;
	std::unique_ptr<LoadedFileCache> _fileCache;
	std::unique_ptr<UserpicsProcess> _userpicsProcess;
	std::unique_ptr<FileProcess> _fileProcess;
	std::unique_ptr<LeftChannelsProcess> _leftChannelsProcess;
	std::unique_ptr<DialogsProcess> _dialogsProcess;
	std::unique_ptr<ChatProcess> _chatProcess;

	rpl::event_stream<RPCError> _errors;

};

} // namespace Export

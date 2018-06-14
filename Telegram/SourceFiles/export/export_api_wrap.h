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
struct FileLocation;
struct PersonalInfo;
struct UserpicsInfo;
struct UserpicsSlice;
struct ContactsList;
struct SessionsList;
struct DialogsInfo;
struct DialogInfo;
struct MessagesSlice;
} // namespace Data

struct Settings;

class ApiWrap {
public:
	ApiWrap(Fn<void(FnMut<void()>)> runner);

	rpl::producer<RPCError> errors() const;

	void startExport(
		const Settings &settings,
		FnMut<void()> done);

	void requestPersonalInfo(FnMut<void(Data::PersonalInfo&&)> done);

	void requestUserpics(
		FnMut<void(Data::UserpicsInfo&&)> start,
		Fn<void(Data::UserpicsSlice&&)> slice,
		FnMut<void()> finish);

	void requestContacts(FnMut<void(Data::ContactsList&&)> done);

	void requestSessions(FnMut<void(Data::SessionsList&&)> done);

	void requestDialogs(
		FnMut<void(const Data::DialogsInfo&)> start,
		Fn<void(const Data::DialogInfo&)> startOne,
		Fn<void(Data::MessagesSlice&&)> sliceOne,
		Fn<void()> finishOne,
		FnMut<void()> finish);

	~ApiWrap();

private:
	void startMainSession(FnMut<void()> done);

	void handleUserpicsSlice(const MTPphotos_Photos &result);
	void loadUserpicsFiles(Data::UserpicsSlice &&slice);
	void loadNextUserpic();
	void loadUserpicDone(const QString &relativePath);
	void finishUserpics();

	void requestSavedContacts();

	void requestDialogsSlice();
	void appendDialogsSlice(Data::DialogsInfo &&info);
	void finishDialogsList();
	void fillDialogsPaths();

	void requestNextDialog();
	void requestMessagesSlice();
	void loadMessagesFiles(Data::MessagesSlice &&slice);
	void loadNextMessageFile();
	void loadMessageFileDone(const QString &relativePath);
	void finishMessages();
	void finishDialogs();

	void loadFile(const Data::File &file, FnMut<void(QString)> done);
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

	struct UserpicsProcess;
	std::unique_ptr<UserpicsProcess> _userpicsProcess;

	struct FileProcess;
	std::unique_ptr<FileProcess> _fileProcess;

	struct DialogsProcess;
	std::unique_ptr<DialogsProcess> _dialogsProcess;

	rpl::event_stream<RPCError> _errors;

};

} // namespace Export

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
} // namespace Data

class ApiWrap {
public:
	ApiWrap(Fn<void(FnMut<void()>)> runner);

	void setFilesBaseFolder(const QString &folder);

	rpl::producer<RPCError> errors() const;

	void requestPersonalInfo(FnMut<void(Data::PersonalInfo&&)> done);

	void requestUserpics(
		FnMut<void(Data::UserpicsInfo&&)> start,
		Fn<void(Data::UserpicsSlice&&)> slice,
		FnMut<void()> finish);

	void requestContacts(FnMut<void(Data::ContactsList&&)> done);

	void requestSessions(FnMut<void(Data::SessionsList&&)> done);

	~ApiWrap();

private:
	void handleUserpicsSlice(const MTPphotos_Photos &result);
	void loadUserpicsFiles(Data::UserpicsSlice &&slice);
	void loadNextUserpic();
	void loadUserpicDone(const QString &relativePath);
	void finishUserpics();

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
	QString _filesFolder;
	MTPInputUser _user = MTP_inputUserSelf();

	struct UserpicsProcess;
	std::unique_ptr<UserpicsProcess> _userpicsProcess;

	struct FileProcess;
	std::unique_ptr<FileProcess> _fileProcess;

	rpl::event_stream<RPCError> _errors;

};

} // namespace Export

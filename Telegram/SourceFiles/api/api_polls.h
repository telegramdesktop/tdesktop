/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "ui/text/text_entity.h"

class ApiWrap;
class HistoryItem;
struct PollData;
struct PollMedia;
namespace Data {
struct StatisticalGraph;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Api {

struct SendAction;

class Polls final {
public:
	explicit Polls(not_null<ApiWrap*> api);

	void create(
		const PollData &data,
		const TextWithEntities &text,
		SendAction action,
		Fn<void()> done,
		Fn<void(bool fileReferenceExpired)> fail);
	void sendVotes(
		FullMsgId itemId,
		const std::vector<QByteArray> &options);
	void addAnswer(
		FullMsgId itemId,
		const TextWithEntities &text,
		const PollMedia &media,
		Fn<void()> done,
		Fn<void(QString)> fail);
	void deleteAnswer(FullMsgId itemId, const QByteArray &option);
	void close(not_null<HistoryItem*> item);
	void reloadResults(not_null<HistoryItem*> item);
	void requestStats(
		FullMsgId itemId,
		Fn<void(Data::StatisticalGraph)> done,
		Fn<void(QString)> fail);

private:
	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_map<FullMsgId, mtpRequestId> _pollVotesRequestIds;
	base::flat_map<FullMsgId, mtpRequestId> _pollAddAnswerRequestIds;
	base::flat_map<FullMsgId, mtpRequestId> _pollCloseRequestIds;
	base::flat_map<FullMsgId, mtpRequestId> _pollReloadRequestIds;

};

} // namespace Api

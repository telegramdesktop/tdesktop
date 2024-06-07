/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Data {
struct WebPageDraft;
} // namespace Data

namespace MTP {
class Error;
} // namespace MTP

namespace Api {

struct SendOptions;
struct RemoteFileInfo;

const auto kDefaultEditMessagesErrors = {
	u"MESSAGE_ID_INVALID"_q,
	u"CHAT_ADMIN_REQUIRED"_q,
	u"MESSAGE_EDIT_TIME_EXPIRED"_q,
};

void RescheduleMessage(
	not_null<HistoryItem*> item,
	SendOptions options);

void EditMessageWithUploadedDocument(
	HistoryItem *item,
	RemoteFileInfo info,
	SendOptions options);

void EditMessageWithUploadedPhoto(
	HistoryItem *item,
	RemoteFileInfo info,
	SendOptions options);

mtpRequestId EditCaption(
	not_null<HistoryItem*> item,
	const TextWithEntities &caption,
	SendOptions options,
	Fn<void()> done,
	Fn<void(const QString &)> fail);

mtpRequestId EditTextMessage(
	not_null<HistoryItem*> item,
	const TextWithEntities &caption,
	Data::WebPageDraft webpage,
	SendOptions options,
	Fn<void(mtpRequestId requestId)> done,
	Fn<void(const QString &error, mtpRequestId requestId)> fail,
	bool spoilered);

} // namespace Api

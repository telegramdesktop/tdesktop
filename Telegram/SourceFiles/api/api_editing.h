/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class RPCError;

namespace Api {

struct SendOptions;

void RescheduleMessage(
	not_null<HistoryItem*> item,
	SendOptions options);

void EditMessageWithUploadedDocument(
	HistoryItem *item,
	const MTPInputFile &file,
	const std::optional<MTPInputFile> &thumb,
	SendOptions options);

void EditMessageWithUploadedPhoto(
	HistoryItem *item,
	const MTPInputFile &file,
	SendOptions options);

mtpRequestId EditCaption(
	not_null<HistoryItem*> item,
	const TextWithEntities &caption,
	Fn<void(const MTPUpdates &)> done,
	Fn<void(const RPCError &)> fail);

mtpRequestId EditTextMessage(
	not_null<HistoryItem*> item,
	const TextWithEntities &caption,
	SendOptions options,
	Fn<void(const MTPUpdates &, mtpRequestId requestId)> done,
	Fn<void(const RPCError &, mtpRequestId requestId)> fail);

} // namespace Api

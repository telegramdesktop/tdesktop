/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class DocumentData;
struct TextWithEntities;

namespace Api {

void SendExistingDocument(
	not_null<History*> history,
	not_null<DocumentData*> document,
	bool silent = false);

void SendExistingDocument(
	not_null<History*> history,
	not_null<DocumentData*> document,
	TextWithEntities caption,
	MsgId replyToId = 0,
	bool silent = false);

void SendExistingPhoto(
	not_null<History*> history,
	not_null<PhotoData*> photo,
	bool silent = false);

void SendExistingPhoto(
	not_null<History*> history,
	not_null<PhotoData*> photo,
	TextWithEntities caption,
	MsgId replyToId = 0,
	bool silent = false);

} // namespace Api

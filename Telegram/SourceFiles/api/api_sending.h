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
	not_null<DocumentData*> document);

void SendExistingDocument(
	not_null<History*> history,
	not_null<DocumentData*> document,
	TextWithEntities caption,
	MsgId replyToId = 0);

void SendExistingPhoto(
	not_null<History*> history,
	not_null<PhotoData*> photo);

void SendExistingPhoto(
	not_null<History*> history,
	not_null<PhotoData*> photo,
	TextWithEntities caption,
	MsgId replyToId = 0);

} // namespace Api

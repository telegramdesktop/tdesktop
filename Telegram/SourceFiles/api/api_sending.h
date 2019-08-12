/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class DocumentData;

namespace Api {

struct MessageToSend;

void SendExistingDocument(
	Api::MessageToSend &&message,
	not_null<DocumentData*> document);

void SendExistingPhoto(
	Api::MessageToSend &&message,
	not_null<PhotoData*> photo);

} // namespace Api

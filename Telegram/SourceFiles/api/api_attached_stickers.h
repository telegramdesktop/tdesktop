/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;
class DocumentData;
class PhotoData;

namespace Window {
class SessionController;
} // namespace Window

namespace Api {

class AttachedStickers final {
public:
	explicit AttachedStickers(not_null<ApiWrap*> api);

	void requestAttachedStickerSets(
		not_null<Window::SessionController*> controller,
		not_null<PhotoData*> photo);

	void requestAttachedStickerSets(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document);

private:
	void request(
		not_null<Window::SessionController*> controller,
		MTPmessages_GetAttachedStickers &&mtpRequest);

	MTP::Sender _api;
	mtpRequestId _requestId = 0;

};

} // namespace Api

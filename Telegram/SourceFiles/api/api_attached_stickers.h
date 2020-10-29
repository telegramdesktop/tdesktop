/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;
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

private:
	MTP::Sender _api;
	mtpRequestId _requestId = 0;

};

} // namespace Api

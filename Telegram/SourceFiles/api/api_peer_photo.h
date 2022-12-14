/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

class ApiWrap;
class PeerData;
class UserData;

namespace Data {
struct FileOrigin;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Api {

class PeerPhoto final {
public:
	using UserPhotoId = PhotoId;
	explicit PeerPhoto(not_null<ApiWrap*> api);

	void upload(not_null<PeerData*> peer, QImage &&image);
	void uploadFallback(not_null<PeerData*> peer, QImage &&image);
	void updateSelf(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin);
	void suggest(not_null<PeerData*> peer, QImage &&image);
	void clear(not_null<PhotoData*> photo);
	void clearPersonal(not_null<UserData*> user);
	void set(not_null<PeerData*> peer, not_null<PhotoData*> photo);

	void requestUserPhotos(not_null<UserData*> user, UserPhotoId afterId);

	// Non-personal photo in case a personal photo is set.
	void registerNonPersonalPhoto(
		not_null<UserData*> user,
		not_null<PhotoData*> photo);
	void unregisterNonPersonalPhoto(not_null<UserData*> user);
	[[nodiscard]] PhotoData *nonPersonalPhoto(
		not_null<UserData*> user) const;

private:
	enum class UploadType {
		Default,
		Suggestion,
		Fallback,
	};

	void ready(const FullMsgId &msgId, const MTPInputFile &file);
	void upload(
		not_null<PeerData*> peer,
		QImage &&image,
		UploadType type);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	struct UploadValue {
		not_null<PeerData*> peer;
		bool fallback = false;
	};

	base::flat_map<FullMsgId, UploadValue> _uploads;
	base::flat_set<FullMsgId> _suggestions;

	base::flat_map<not_null<UserData*>, mtpRequestId> _userPhotosRequests;

	base::flat_map<
		not_null<UserData*>,
		not_null<PhotoData*>> _nonPersonalPhotos;

};

} // namespace Api

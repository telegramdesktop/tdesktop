/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "mtproto/sender.h"

class DocumentData;
class PhotoData;
class UserData;

namespace Api {

using BotPreviewMedia = std::variant<
	not_null<PhotoData*>,
	not_null<DocumentData*>>;

struct BotMediaPreviewList {
	std::vector<BotPreviewMedia> media;
	std::vector<QString> languages;
};

class BotMediaPreviews final : public base::has_weak_ptr {
public:
	using Done = Fn<void()>;
	using Fail = Fn<void(QString)>;
	using Published = Fn<void(std::optional<BotPreviewMedia>)>;

	explicit BotMediaPreviews(not_null<UserData*> bot);

	void load(
		const std::optional<QString> &language,
		Fn<void(BotMediaPreviewList)> done,
		Fail fail);
	void save(
		const QString &language,
		std::optional<BotPreviewMedia> replaced,
		MTPInputMedia media,
		Published done,
		Fail fail);
	void remove(
		const QString &language,
		std::vector<BotPreviewMedia> media,
		Done done,
		Fail fail);
	void reorder(
		const QString &language,
		std::vector<BotPreviewMedia> order,
		Done done,
		Fail fail);
private:
	[[nodiscard]] std::vector<BotPreviewMedia> parse(
		const MTPVector<MTPBotPreviewMedia> &list) const;
	[[nodiscard]] std::optional<BotPreviewMedia> parse(
		const MTPMessageMedia &media) const;
	void saveUploaded(
		const QString &language,
		std::optional<BotPreviewMedia> replaced,
		MTPInputMedia media,
		Fn<void(const MTPBotPreviewMedia&)> done,
		Fail fail);
	template <typename Request, typename Callback>
	void sendMutation(
		Request request,
		const QString &language,
		Callback done,
		Fail fail,
		bool refreshReferences);

	const not_null<UserData*> _bot;
	MTP::Sender _api;

};

} // namespace Api

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/custom_emoji_instance.h"

struct StickerSetIdentifier;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
struct CustomEmojiId;
class CustomEmojiLoader;

class CustomEmojiManager final {
public:
	CustomEmojiManager(not_null<Session*> owner);
	~CustomEmojiManager();

	[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> create(
		const QString &data,
		Fn<void()> update);

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] Session &owner() const;

private:
	struct Set {
		int32 hash = 0;
		mtpRequestId requestId = 0;
		base::flat_set<uint64> documents;
		base::flat_set<uint64> waiting;
	};

	void requestSetIfNeeded(const CustomEmojiId &id);

	const not_null<Session*> _owner;

	base::flat_map<uint64, base::flat_map<
		uint64,
		std::unique_ptr<Ui::CustomEmoji::Instance>>> _instances;
	base::flat_map<uint64, Set> _sets;
	base::flat_map<
		uint64,
		std::vector<base::weak_ptr<CustomEmojiLoader>>> _loaders;

};

void FillTestCustomEmoji(
	not_null<Main::Session*> session,
	TextWithEntities &text);

} // namespace Data

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

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
	const not_null<Session*> _owner;

};

void FillTestCustomEmoji(
	not_null<Main::Session*> session,
	TextWithEntities &text);

} // namespace Data

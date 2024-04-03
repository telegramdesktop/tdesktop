/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class SessionShow;
} // namespace Main

namespace Data {

class Faq final {
public:
	constexpr Faq() = default;

	void open(std::shared_ptr<Main::SessionShow> show);

private:
	WebPageId _id = 0;

};

} // namespace Data

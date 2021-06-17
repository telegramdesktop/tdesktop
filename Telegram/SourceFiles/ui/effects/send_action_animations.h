/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {
enum class SendProgressType;
} // namespace Api

namespace Ui {

class SendActionAnimation {
public:
	using Type = Api::SendProgressType;
	class Impl;

	SendActionAnimation();
	~SendActionAnimation();

	void start(Type type);
	void tryToFinish();

	int width() const;
	void paint(Painter &p, style::color color, int x, int y, int outerWidth, crl::time ms) const;

	explicit operator bool() const {
		return _impl != nullptr;
	}

	static void PaintSpeakingIdle(Painter &p, style::color color, int x, int y, int outerWidth);

private:
	[[nodiscard]] static std::unique_ptr<Impl> CreateByType(Type type);

	std::unique_ptr<Impl> _impl;

};

} // namespace Ui

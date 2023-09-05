/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
	int widthNoMargins() const;
	void paint(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth,
		crl::time ms) const;

	explicit operator bool() const {
		return _impl != nullptr;
	}

	static void PaintSpeakingIdle(
		QPainter &p,
		style::color color,
		int x,
		int y,
		int outerWidth);

private:
	[[nodiscard]] static std::unique_ptr<Impl> CreateByType(Type type);

	std::unique_ptr<Impl> _impl;

};

} // namespace Ui

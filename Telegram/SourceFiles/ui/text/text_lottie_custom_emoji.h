/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_custom_emoji.h"

namespace Lottie {
struct IconDescriptor;
class Icon;
} // namespace Lottie

namespace Ui::Text {

class LottieCustomEmoji final : public CustomEmoji {
public:
	explicit LottieCustomEmoji(Lottie::IconDescriptor &&descriptor);
	LottieCustomEmoji(
		Lottie::IconDescriptor &&descriptor,
		Fn<void()> repaint);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	void startAnimation();

	QString _entityData;
	int _width = 0;
	std::unique_ptr<Lottie::Icon> _icon;
	Fn<void()> _repaint;

};

[[nodiscard]] QString LottieEmojiData(Lottie::IconDescriptor);
[[nodiscard]] TextWithEntities LottieEmoji(Lottie::IconDescriptor);
[[nodiscard]] MarkedContext LottieEmojiContext(Lottie::IconDescriptor);

} // namespace Ui::Text
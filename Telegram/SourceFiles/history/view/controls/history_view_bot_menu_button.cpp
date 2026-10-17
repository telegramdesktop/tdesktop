/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_bot_menu_button.h"

#include "base/algorithm.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "ui/emoji_config.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_lottie_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kBotMenuSizeAnimation = crl::time(150);

[[nodiscard]] QString FirstEmoji(const QString &s) {
	const auto begin = s.data();
	const auto end = begin + s.size();
	for (auto ch = begin; ch != end; ch++) {
		auto length = 0;
		if (const auto e = Ui::Emoji::Find(ch, end, &length)) {
			return e->text();
		}
	}
	return QString();
}

[[nodiscard]] Lottie::IconDescriptor BotMenuIconDescriptor() {
	return {
		.name = u"bot_menu"_q,
		.color = &st::historyBotMenuButton.textFg,
		.sizeOverride = Size(st::historyBotMenuIconSize),
		.colorizeUsingAlpha = true,
	};
}

[[nodiscard]] TextWithEntities BotMenuIconText(const QString &menuText) {
	const auto data = Ui::Text::LottieEmojiData(BotMenuIconDescriptor());
	auto result = tr::marked(menuText.isEmpty()
		? tr::lng_bot_menu_button(tr::now)
		: menuText);
	result.entities.push_back({
		EntityType::CustomEmoji,
		0,
		int(result.text.size()),
		data,
	});
	return result;
}

[[nodiscard]] TextWithEntities BotMenuTextContent(
		const QString &menuText,
		bool small) {
	if (small) {
		if (const auto e = FirstEmoji(menuText); !e.isEmpty()) {
			return tr::marked(e);
		}
		return BotMenuIconText(menuText);
	} else if (!menuText.isEmpty()) {
		return tr::marked(menuText);
	}
	return tr::marked(tr::lng_bot_menu_button(tr::now));
}

[[nodiscard]] int BotMenuIconVerticalShift() {
	const auto &st = st::historyBotMenuButton;
	const auto emojiY = (st.style.font->height - st::emojiSize) / 2;
	const auto skip = (st::emojiSize
		- Ui::Text::AdjustCustomEmojiSize(st::emojiSize)) / 2;
	return ((st.height - st::historyBotMenuIconSize) / 2)
		- (st.textTop + emojiY + skip);
}

class BotMenuIconEmoji final : public Ui::Text::CustomEmoji {
public:
	int width() override {
		return st::historyBotMenuIconSize;
	}

	QString entityData() override {
		return u"bot_menu"_q;
	}

	void paint(QPainter &p, const Context &context) override {
		SharedIcon().paint(
			p,
			context.position.x(),
			context.position.y(),
			context.textColor);
	}

	void unload() override {
	}

	bool ready() override {
		return SharedIcon().valid();
	}

	bool readyInDefaultState() override {
		return ready();
	}

private:
	static Lottie::Icon &SharedIcon() {
		static const auto result = Lottie::MakeIcon({
			.name = u"bot_menu"_q,
			.color = &st::historyBotMenuButton.textFg,
			.sizeOverride = Size(st::historyBotMenuIconSize),
			.colorizeUsingAlpha = true,
		});
		return *result;
	}

};

[[nodiscard]] Ui::Text::MarkedContext BotMenuIconContext(
		Ui::Text::CustomEmojiFactory other) {
	auto context = Ui::Text::MarkedContext();
	const auto shift = QPoint(0, BotMenuIconVerticalShift());
	context.customEmojiFactory = [=](
			QStringView data,
			const Ui::Text::MarkedContext &parent)
			-> std::unique_ptr<Ui::Text::CustomEmoji> {
		if (data == u"bot_menu"_q) {
			return std::make_unique<Ui::Text::ShiftedEmoji>(
				std::make_unique<BotMenuIconEmoji>(),
				shift);
		}
		return other ? other(data, parent) : nullptr;
	};
	return context;
}

} // namespace

BotMenuButton::BotMenuButton(
	QWidget *parent,
	const QString &menuText,
	bool small,
	Fn<void()> clicked,
	Fn<void()> widthChanged,
	Ui::Text::CustomEmojiFactory otherEmoji)
: Ui::RoundButton(parent, rpl::single(QString()), st::historyBotMenuButton)
, _otherEmoji(std::move(otherEmoji))
, _text(menuText)
, _small(small) {
	setFullRadius(true);
	setContext(BotMenuIconContext(_otherEmoji));
	setClickedCallback(std::move(clicked));
	widthValue(
	) | rpl::on_next([=](int width) {
		if (width > st::historyBotMenuMaxWidth) {
			setFullWidth(st::historyBotMenuMaxWidth);
		} else {
			widthChanged();
		}
	}, lifetime());
	setFullWidth(_small ? st::historyBotMenuIconWidth : 0);
	setText(rpl::single(BotMenuTextContent(_text, _small)));
}

bool BotMenuButton::refresh(const QString &menuText, bool small) {
	if ((_text == menuText) && (_small == small)) {
		return false;
	}
	const auto wasText = _text;
	const auto wasSmall = _small;
	_text = menuText;
	_small = small;
	if (wasSmall == small) {
		_widthAnimation.stop();
		_contentFade.stop();
		_fading = Ui::Text::String();
		setFullWidth(_small ? st::historyBotMenuIconWidth : 0);
		setText(rpl::single(BotMenuTextContent(_text, _small)));
	} else {
		animateSize(wasText, wasSmall);
	}
	return true;
}

void BotMenuButton::animateSize(const QString &wasText, bool wasSmall) {
	_widthAnimation.stop();
	const auto &st = st::historyBotMenuButton;
	auto target = st::historyBotMenuIconWidth;
	if (_small) {
		setFullWidth(width());
	} else {
		auto measure = Ui::Text::String();
		measure.setMarkedText(
			st.style,
			BotMenuTextContent(_text, false),
			kMarkupTextOptions,
			BotMenuIconContext(_otherEmoji));
		target = std::max(std::min(
			measure.maxWidth() - st.width,
			st::historyBotMenuMaxWidth), target);
	}
	const auto delta = st.height - st.style.font->height;
	const auto pads = st.padding.left() + st.padding.right();
	const auto availFor = [&](int box, int textWidth) {
		return (box < textWidth + delta)
			? std::max(box - delta, 1)
			: std::min(textWidth, box - pads);
	};
	const auto continuing = _contentFade.animating();
	const auto wasFade = continuing ? _contentFade.value(1.) : 0.;
	_contentFade.stop();
	if (continuing) {
		_fading = std::move(_appearing);
		_fadeFromLeft = _fadeToLeft;
		_fadeFromWidth = _fadeToWidth;
	} else {
		auto fading = BotMenuTextContent(wasText, wasSmall);
		_fading.setMarkedText(
			st.style,
			fading,
			kMarkupTextOptions,
			BotMenuIconContext(_otherEmoji));
		_fadeFromLeft = st.padding.left()
			+ ((width() - _fading.maxWidth() - pads) / 2);
		_fadeFromWidth = availFor(width(), _fading.maxWidth());
	}
	_appearing.setMarkedText(
		st.style,
		BotMenuTextContent(_text, _small),
		kMarkupTextOptions,
		BotMenuIconContext(_otherEmoji));
	_fadeToLeft = st.padding.left()
		+ ((target - _appearing.maxWidth() - pads) / 2);
	_fadeToWidth = availFor(target, _appearing.maxWidth());
	setFullWidth(width());
	setText(rpl::single(TextWithEntities()));
	const auto from = width();
	if (from != target) {
		_widthAnimation.start([=](float64 value) {
			setFullWidth(int(base::SafeRound(value)));
		}, from, target, kBotMenuSizeAnimation, anim::sineInOut);
	}
	_widthAnimation.setFinishedCallback([=] {
		if (!_small) {
			setFullWidth(0);
		}
	});
	_contentFade.start(
		[=](float64 value) {
			if (value == 1.) {
				_fading = Ui::Text::String();
				setFullWidth(_small ? st::historyBotMenuIconWidth : 0);
				setText(rpl::single(BotMenuTextContent(_text, _small)));
			}
			update();
		},
		continuing ? (1. - wasFade) : 0.,
		1.,
		kBotMenuSizeAnimation,
		anim::linear);
}

void BotMenuButton::paintEvent(QPaintEvent *e) {
	Ui::RoundButton::paintEvent(e);
	const auto fade = _contentFade.value(1.);
	if (fade >= 1.) {
		return;
	}
	const auto &st = st::historyBotMenuButton;
	const auto over = isOver() || isDown();
	Painter p(this);
	p.setPen(over ? st.textFgOver : st.textFg);
	auto local = st::defaultTextPalette;
	local.linkFg = over ? st.numbersTextFgOver : st.numbersTextFg;
	const auto textTop = st.padding.top() + st.textTop;
	p.setOpacity(1. - fade);
	_fading.draw(p, {
		.position = { _fadeFromLeft, textTop },
		.availableWidth = _fadeFromWidth,
		.palette = &local,
		.elisionLines = 1,
	});
	p.setOpacity(fade);
	_appearing.draw(p, {
		.position = { _fadeToLeft, textTop },
		.availableWidth = _fadeToWidth,
		.palette = &local,
		.elisionLines = 1,
	});
	p.setOpacity(1.);
}

} // namespace HistoryView

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/chats_filter_tag.h"

#include "ui/text/text_custom_emoji.h"
#include "ui/emoji_config.h"
#include "ui/integration.h"
#include "ui/painter.h"
#include "styles/style_dialogs.h"

namespace Ui {
namespace {

class ScaledSimpleEmoji final : public Ui::Text::CustomEmoji {
public:
	ScaledSimpleEmoji(EmojiPtr emoji);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const EmojiPtr _emoji;
	QImage _frame;
	QPoint _shift;

};

class ScaledCustomEmoji final : public Ui::Text::CustomEmoji {
public:
	ScaledCustomEmoji(std::unique_ptr<Ui::Text::CustomEmoji> wrapped);

	int width() override;
	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const std::unique_ptr<Ui::Text::CustomEmoji> _wrapped;
	QImage _frame;
	QPoint _shift;

};

[[nodiscard]] int ScaledSize() {
	return st::dialogRowFilterTagStyle.font->height - 2 * st::lineWidth;
}

ScaledSimpleEmoji::ScaledSimpleEmoji(EmojiPtr emoji)
: _emoji(emoji) {
}

int ScaledSimpleEmoji::width() {
	return ScaledSize();
}

QString ScaledSimpleEmoji::entityData() {
	return u"scaled-simple:"_q + _emoji->text();
}

void ScaledSimpleEmoji::paint(QPainter &p, const Context &context) {
	if (_frame.isNull()) {
		const auto adjusted = Text::AdjustCustomEmojiSize(st::emojiSize);
		const auto xskip = (st::emojiSize - adjusted) / 2;
		const auto yskip = xskip + (width() - st::emojiSize) / 2;
		_shift = { xskip, yskip };

		const auto ratio = style::DevicePixelRatio();
		const auto large = Emoji::GetSizeLarge();
		const auto size = QSize(large, large);
		_frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
		_frame.fill(Qt::transparent);

		auto p = QPainter(&_frame);
		Emoji::Draw(p, _emoji, large, 0, 0);
		p.end();

		_frame = _frame.scaled(
			QSize(width(), width()) * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}

	p.drawImage(context.position - _shift, _frame);
}

void ScaledSimpleEmoji::unload() {
}

bool ScaledSimpleEmoji::ready() {
	return true;
}

bool ScaledSimpleEmoji::readyInDefaultState() {
	return true;
}

ScaledCustomEmoji::ScaledCustomEmoji(
	std::unique_ptr<Ui::Text::CustomEmoji> wrapped)
: _wrapped(std::move(wrapped)) {
}

int ScaledCustomEmoji::width() {
	return ScaledSize();
}

QString ScaledCustomEmoji::entityData() {
	return u"scaled-custom:"_q + _wrapped->entityData();
}

void ScaledCustomEmoji::paint(QPainter &p, const Context &context) {
	if (_frame.isNull()) {
		if (!_wrapped->ready()) {
			return;
		}
		const auto ratio = style::DevicePixelRatio();
		const auto large = Emoji::GetSizeLarge();
		const auto largeadjust = Text::AdjustCustomEmojiSize(large / ratio);
		const auto size = QSize(largeadjust, largeadjust) * ratio;
		_frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
		_frame.setDevicePixelRatio(ratio);
		_frame.fill(Qt::transparent);

		auto p = QPainter(&_frame);
		p.translate(-context.position);
		const auto was = context.internal.forceFirstFrame;
		context.internal.forceFirstFrame = true;
		_wrapped->paint(p, context);
		context.internal.forceFirstFrame = was;
		p.end();

		const auto smalladjust = Text::AdjustCustomEmojiSize(width());
		_frame = _frame.scaled(
			QSize(smalladjust, smalladjust) * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		_wrapped->unload();

		const auto adjusted = Text::AdjustCustomEmojiSize(st::emojiSize);
		const auto xskip = (st::emojiSize - adjusted) / 2;
		const auto yskip = xskip + (width() - st::emojiSize) / 2;

		const auto add = (width() - smalladjust) / 2;
		_shift = QPoint(xskip, yskip) - QPoint(add, add);
	}
	p.drawImage(context.position - _shift, _frame);
}

void ScaledCustomEmoji::unload() {
	_wrapped->unload();
}

bool ScaledCustomEmoji::ready() {
	return !_frame.isNull() || _wrapped->ready();
}

bool ScaledCustomEmoji::readyInDefaultState() {
	return !_frame.isNull() || _wrapped->ready();
}

[[nodiscard]] TextWithEntities PrepareSmallEmojiText(
		TextWithEntities text,
		ChatsFilterTagContext &context) {
	auto i = text.entities.begin();
	auto ch = text.text.constData();
	auto &integration = Integration::Instance();
	context.loading = false;
	const auto end = text.text.constData() + text.text.size();
	const auto adjust = [&](EntityInText &entity) {
		if (entity.type() != EntityType::CustomEmoji) {
			return;
		}
		const auto data = entity.data();
		if (data.startsWith(u"scaled-simple:"_q)) {
			return;
		}
		auto &emoji = context.emoji[data];
		if (!emoji) {
			emoji = integration.createCustomEmoji(
				data,
				context.textContext);
		}
		if (!emoji->ready()) {
			context.loading = true;
		}
		entity = EntityInText(
			entity.type(),
			entity.offset(),
			entity.length(),
			u"scaled-custom:"_q + entity.data());
	};
	const auto till = [](EntityInText &entity) {
		return entity.offset() + entity.length();
	};
	while (ch != end) {
		auto emojiLength = 0;
		if (const auto emoji = Ui::Emoji::Find(ch, end, &emojiLength)) {
			const auto f = int(ch - text.text.constData());
			const auto l = f + emojiLength;
			while (i != text.entities.end() && till(*i) <= f) {
				adjust(*i);
				++i;
			}

			ch += emojiLength;
			if (i != text.entities.end() && i->offset() < l) {
				continue;
			}
			i = text.entities.insert(i, EntityInText{
				EntityType::CustomEmoji,
				f,
				emojiLength,
				u"scaled-simple:"_q + emoji->text(),
			});
		} else {
			++ch;
		}
	}
	for (; i != text.entities.end(); ++i) {
		adjust(*i);
	}
	return text;
}

} // namespace

QImage ChatsFilterTag(
		const TextWithEntities &text,
		ChatsFilterTagContext &context) {
	const auto &roundedFont = st::dialogRowFilterTagStyle.font;
	const auto additionalWidth = roundedFont->spacew * 3;
	auto rich = Text::String(
		st::dialogRowFilterTagStyle,
		PrepareSmallEmojiText(text, context),
		kMarkupTextOptions,
		kQFixedMax,
		context.textContext);
	const auto roundedWidth = rich.maxWidth() + additionalWidth;
	const auto rect = QRect(0, 0, roundedWidth, roundedFont->height);
	auto cache = QImage(
		rect.size() * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(style::DevicePixelRatio());
	cache.fill(Qt::transparent);
	{
		auto p = QPainter(&cache);
		const auto pen = QPen(context.active
			? st::dialogsBgActive->c
			: context.color);
		p.setPen(Qt::NoPen);
		p.setBrush(context.active
			? st::dialogsTextFgActive->c
			: anim::with_alpha(pen.color(), .15));
		{
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = roundedFont->height / 3.;
			p.drawRoundedRect(rect, radius, radius);
		}
		p.setPen(pen);
		p.setFont(roundedFont);
		const auto dx = (rect.width() - rich.maxWidth()) / 2;
		const auto dy = (rect.height() - roundedFont->height) / 2;
		rich.draw(p, {
			.position = rect.topLeft() + QPoint(dx, dy),
			.availableWidth = rich.maxWidth(),
		});
	}
	return cache;
}

std::unique_ptr<Text::CustomEmoji> MakeScaledSimpleEmoji(EmojiPtr emoji) {
	return std::make_unique<ScaledSimpleEmoji>(emoji);
}

std::unique_ptr<Text::CustomEmoji> MakeScaledCustomEmoji(
		std::unique_ptr<Text::CustomEmoji> wrapped) {
	return std::make_unique<ScaledCustomEmoji>(std::move(wrapped));
}

} // namespace Ui

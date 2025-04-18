/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/gift_resale_filter.h"

#include "ui/effects/ripple_animation.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/painter.h"
#include "styles/style_credits.h" // giftBoxResaleColorSize
#include "styles/style_media_player.h" // mediaPlayerMenuCheck

namespace Ui {
namespace {

[[nodiscard]] Text::MarkedContext WithRepaint(
		const Text::MarkedContext &context,
		Fn<void()> repaint) {
	auto result = context;
	result.repaint = std::move(repaint);
	return result;
}

[[nodiscard]] QString SerializeColorData(const QColor &color) {
	return u"color:%1,%2,%3,%4"_q
		.arg(color.red())
		.arg(color.green())
		.arg(color.blue())
		.arg(color.alpha());
}

[[nodiscard]] bool IsColorData(QStringView data) {
	return data.startsWith(u"color:"_q);
}

[[nodiscard]] QColor ParseColorData(QStringView data) {
	Expects(data.size() > 12);

	const auto parts = data.mid(6).split(',');
	Assert(parts.size() == 4);
	return QColor(
		parts[0].toInt(),
		parts[1].toInt(),
		parts[2].toInt(),
		parts[3].toInt());
}

} // namespace

GiftResaleFilterAction::GiftResaleFilterAction(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	const TextWithEntities &text,
	const Text::MarkedContext &context,
	QString iconEmojiData,
	const style::icon *icon)
: Action(parent, st, new QAction(parent), icon, icon)
, _iconEmoji(iconEmojiData.isEmpty()
	? nullptr
	: context.customEmojiFactory(
		iconEmojiData,
		WithRepaint(context, [=] { update(); }))) {
	setMarkedText(text, QString(), context);
}

void GiftResaleFilterAction::paintEvent(QPaintEvent *e) {
	Action::paintEvent(e);

	Painter p(this);

	const auto enabled = isEnabled();
	const auto selected = isSelected();
	const auto fg = selected
		? st().itemFgOver
		: enabled
		? st().itemFg
		: st().itemFgDisabled;
	if (const auto emoji = _iconEmoji.get()) {
		const auto x = st().itemIconPosition.x();
		const auto y = (height() - st::emojiSize) / 2;
		emoji->paint(p, {
			.textColor = fg->c,
			.position = { x, y },
		});
	}
	if (_checked) {
		const auto &icon = st::mediaPlayerMenuCheck;
		const auto skip = st().itemRightSkip;
		const auto left = width() - skip - icon.width();
		const auto top = (height() - icon.height()) / 2;
		icon.paint(p, left, top, width());
	}
}

void GiftResaleFilterAction::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		update();
	}
}

GiftResaleColorEmoji::GiftResaleColorEmoji(QStringView data)
: _color(ParseColorData(data)) {
}

bool GiftResaleColorEmoji::Owns(QStringView data) {
	return IsColorData(data);
}

QString GiftResaleColorEmoji::DataFor(QColor color) {
	return SerializeColorData(color);
}

int GiftResaleColorEmoji::width() {
	return st::giftBoxResaleColorSize;
}

QString GiftResaleColorEmoji::entityData() {
	return DataFor(_color);
}

void GiftResaleColorEmoji::paint(QPainter &p, const Context &context) {
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(_color);
	p.setPen(Qt::NoPen);
	p.drawEllipse(
		context.position.x(),
		context.position.y() + st::giftBoxResaleColorTop,
		width(),
		width());
}

void GiftResaleColorEmoji::unload() {
}

bool GiftResaleColorEmoji::ready() {
	return true;
}

bool GiftResaleColorEmoji::readyInDefaultState() {
	return true;
}

} // namespace Ui

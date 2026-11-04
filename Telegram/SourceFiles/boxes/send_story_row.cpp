/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/send_story_row.h"

#include "data/data_peer.h"
#include "ui/painter.h"
#include "ui/widgets/checkbox.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"

SendStoryRow::SendStoryRow(QWidget *parent, SendStoryRowContent content)
: RippleButton(parent, (content.style
	? *content.style
	: st::sendStoryRowStyle).ripple)
, _content(std::move(content))
, _st(_content.style ? *_content.style : st::sendStoryRowStyle)
, _radio(_content.radio
	? std::make_unique<Ui::RadioView>(
		_st.radio,
		false,
		[=] { update(); })
	: nullptr) {
	_title.setText(st::storyRowTitleStyle, _content.title);
	_subtitle.setText(st::storyRowSubtitleStyle, _content.subtitle);
	resize(width(), rowHeight());
}

SendStoryRow::~SendStoryRow() = default;

void SendStoryRow::setChecked(bool checked, anim::type animated) {
	if (_radio) {
		_radio->setChecked(checked, animated);
	}
}

bool SendStoryRow::checked() const {
	return _radio && _radio->checked();
}

void SendStoryRow::setSubtitle(const QString &subtitle) {
	_content.subtitle = subtitle;
	_subtitle.setText(st::storyRowSubtitleStyle, subtitle);
	update();
}

void SendStoryRow::setPeer(
		not_null<PeerData*> peer,
		const QString &subtitle) {
	_content.peer = peer;
	_content.title = peer->name();
	_userpic = {};
	_title.setText(st::storyRowTitleStyle, _content.title);
	setSubtitle(subtitle);
}

void SendStoryRow::setDivider(bool divider) {
	_content.divider = divider;
	update();
}

QString SendStoryRow::subtitle() const {
	return _content.subtitle;
}

bool SendStoryRow::hasRadio() const {
	return (_radio != nullptr);
}

int SendStoryRow::rowHeight() const {
	return (_content.chevron && !_content.radio)
		? st::storyRowSendAsHeight
		: st::storyRowHeight;
}

int SendStoryRow::iconLeft() const {
	return _radio ? st::storyRowIconLeft : st::storyRowIconLeftNoRadio;
}

int SendStoryRow::textLeft() const {
	return _radio ? st::storyRowTextLeft : st::storyRowTextLeftNoRadio;
}

int SendStoryRow::resizeGetHeight(int newWidth) {
	return rowHeight();
}

void SendStoryRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	paintRipple(p, 0, 0);

	if (_radio) {
		const auto size = _radio->getSize();
		_radio->paint(
			p,
			st::storyRowRadioLeft,
			(height() - size.height()) / 2,
			width());
	}
	paintIcon(p);

	const auto left = textLeft();
	const auto right = _content.chevron
		? (st::storyRowChevronRight + st::storyRowChevronSize)
		: st::storyRowTextRight;
	const auto available = width() - left - right;
	if (available <= 0) {
		return;
	}
	p.setPen(_st.titleFg);
	_title.drawLeftElided(p, left, st::storyRowTitleTop, available, width());

	if (!_subtitle.isEmpty()) {
		const auto top = st::storyRowSubtitleTop;
		const auto arrow = _content.subtitleLink
			? (st::storyRowArrowSkip + st::storyRowArrowSize / 2)
			: 0;
		const auto textWidth = std::min(
			_subtitle.maxWidth(),
			available - arrow);
		p.setPen(_content.subtitleLink
			? _st.subtitleLinkFg
			: _st.subtitleFg);
		_subtitle.drawLeftElided(p, left, top, textWidth, width());
		if (_content.subtitleLink) {
			paintSubtitleArrow(p, left + textWidth, top);
		}
	}
	if (_content.chevron) {
		paintChevron(p);
	}
	if (_content.divider) {
		p.fillRect(
			left,
			height() - st::lineWidth,
			width() - left,
			st::lineWidth,
			_st.dividerBg);
	}
}

void SendStoryRow::paintIcon(QPainter &p) {
	const auto size = st::storyRowIconSize;
	const auto x = iconLeft();
	const auto y = (height() - size) / 2;
	if (const auto &icon = _content.icon) {
		auto hq = PainterHighQualityEnabler(p);
		auto gradient = QLinearGradient(0, y, 0, y + size);
		gradient.setStops({
			{ 0., (*icon->bg1)->c },
			{ 1., (*icon->bg2)->c },
		});
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawEllipse(x, y, size, size);
		icon->icon->paintInCenter(p, QRect(x, y, size, size));
	} else if (const auto peer = _content.peer) {
		peer->paintUserpicLeft(p, _userpic, x, y, width(), size);
	}
}

void SendStoryRow::paintSubtitleArrow(QPainter &p, int left, int top) {
	auto hq = PainterHighQualityEnabler(p);
	const auto size = st::storyRowArrowSize;
	const auto x = left + st::storyRowArrowSkip;
	const auto center = top + st::storyRowSubtitleStyle.font->height / 2;
	auto path = QPainterPath();
	path.moveTo(x, center - size / 2.);
	path.lineTo(x + size / 2., center);
	path.lineTo(x, center + size / 2.);
	auto pen = QPen(_st.subtitleLinkFg->c);
	pen.setWidthF(float64(st::storyRowArrowStroke));
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

void SendStoryRow::paintChevron(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	const auto size = st::storyRowChevronSize;
	const auto x = width() - st::storyRowChevronRight;
	const auto center = height() / 2.;
	auto path = QPainterPath();
	path.moveTo(x, center - size);
	path.lineTo(x + size, center);
	path.lineTo(x, center + size);
	auto color = _st.titleFg->c;
	color.setAlphaF(color.alphaF() * st::storyRowChevronOpacity);
	auto pen = QPen(color);
	pen.setWidthF(float64(st::storyRowChevronStroke));
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
}

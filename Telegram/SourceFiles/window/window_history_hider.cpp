/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_history_hider.h"

#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/cached_round_corners.h"
#include "mainwidget.h"
#include "styles/style_layers.h"
#include "styles/style_chat.h"

namespace Window {

HistoryHider::HistoryHider(
	QWidget *parent,
	const QString &text,
	Fn<bool(PeerId)> confirm,
	rpl::producer<bool> oneColumnValue)
: RpWidget(parent)
, _text(text)
, _confirm(std::move(confirm)) {
	Lang::Updated(
	) | rpl::start_with_next([=] {
		refreshLang();
	}, lifetime());

	_chooseWidth = st::historyForwardChooseFont->width(_text);

	resizeEvent(0);
	_a_opacity.start([this] { update(); }, 0., 1., st::boxDuration);

	std::move(
		oneColumnValue
	) | rpl::start_with_next([=](bool oneColumn) {
		_isOneColumn = oneColumn;
	}, lifetime());
}

void HistoryHider::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void HistoryHider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto opacity = _a_opacity.value(_hiding ? 0. : 1.);
	if (opacity == 0.) {
		if (_hiding) {
			_hidden.fire({});
		}
		return;
	}

	p.setOpacity(opacity);
	p.fillRect(rect(), st::layerBg);
	p.setFont(st::historyForwardChooseFont);
	auto w = st::historyForwardChooseMargins.left() + _chooseWidth + st::historyForwardChooseMargins.right();
	auto h = st::historyForwardChooseMargins.top() + st::historyForwardChooseFont->height + st::historyForwardChooseMargins.bottom();
	Ui::FillRoundRect(p, (width() - w) / 2, (height() - h) / 2, w, h, st::historyForwardChooseBg, Ui::ForwardCorners);

	p.setPen(st::historyForwardChooseFg);
	p.drawText(_box, _text, QTextOption(style::al_center));
}

void HistoryHider::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		startHide();
	}
}

void HistoryHider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!_box.contains(e->pos())) {
			startHide();
		}
	}
}

void HistoryHider::startHide() {
	if (_hiding) return;

	_hiding = true;
	if (_isOneColumn) {
		crl::on_main(this, [=] { _hidden.fire({}); });
	} else {
		_a_opacity.start([=] { animationCallback(); }, 1., 0., st::boxDuration);
	}
}

void HistoryHider::animationCallback() {
	update();
	if (!_a_opacity.animating() && _hiding) {
		crl::on_main(this, [=] { _hidden.fire({}); });
	}
}

void HistoryHider::confirm() {
	_confirmed.fire({});
}

rpl::producer<> HistoryHider::confirmed() const {
	return _confirmed.events();
}

rpl::producer<> HistoryHider::hidden() const {
	return _hidden.events();
}

void HistoryHider::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void HistoryHider::updateControlsGeometry() {
	auto w = st::boxWidth;
	auto h = st::boxPadding.top() + st::boxPadding.bottom();
	h += st::historyForwardChooseFont->height;
	_box = QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void HistoryHider::offerPeer(PeerId peer) {
	if (_confirm(peer)) {
		startHide();
	}
}

HistoryHider::~HistoryHider() {
}

} // namespace Window

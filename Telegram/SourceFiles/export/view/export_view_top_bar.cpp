/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_top_bar.h"

#include "export/view/export_view_content.h"
#include "ui/rect.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "lang/lang_keys.h"
#include "styles/style_export.h"
#include "styles/style_media_player.h"

namespace Export {
namespace View {

TopBar::TopBar(QWidget *parent, Content &&content)
: RpWidget(parent)
, _infoLeft(this, st::exportTopBarLabel)
, _infoMiddle(this, st::exportTopBarLabel)
, _infoRight(this, st::exportTopBarLabel)
, _shadow(this)
, _progress(this, st::mediaPlayerPlayback)
, _button(this) {
	resize(width(), st::mediaPlayerHeight + st::lineWidth);
	_progress->setAttribute(Qt::WA_TransparentForMouseEvents);
	updateData(std::move(content));
}

rpl::producer<Qt::MouseButton> TopBar::clicks() const {
	return _button->clicks();
}

void TopBar::resizeToWidthInfo(int w) {
	if (w <= 0) {
		return;
	}
	const auto &infoFont = st::mediaPlayerName.style.font;
	const auto infoTop = st::mediaPlayerNameTop - infoFont->ascent;
	const auto padding = st::mediaPlayerPlayLeft + st::mediaPlayerPadding;
	_infoLeft->moveToLeft(padding, infoTop);
	auto availableWidth = w;
	availableWidth -= rect::right(_infoLeft);
	availableWidth -= padding;
	_infoMiddle->resizeToWidth(_infoMiddle->naturalWidth());
	_infoRight->resizeToWidth(_infoRight->naturalWidth());
	if (_infoMiddle->naturalWidth() > availableWidth) {
		_infoRight->moveToLeft(
			w - padding - _infoRight->width(),
			infoTop);
		_infoMiddle->resizeToWidth(_infoRight->x()
			- rect::right(_infoLeft)
			- infoFont->spacew * 2);
		_infoMiddle->moveToLeft(
			rect::right(_infoLeft) + infoFont->spacew,
			infoTop);
	} else {
		_infoMiddle->moveToLeft(
			rect::right(_infoLeft) + infoFont->spacew,
			infoTop);
		_infoRight->moveToLeft(
			rect::right(_infoMiddle) + infoFont->spacew,
			infoTop);
	}
}

void TopBar::updateData(Content &&content) {
	if (content.rows.empty()) {
		return;
	}
	const auto &row = content.rows[0];
	_infoLeft->setMarkedText(
		tr::lng_export_progress_title(tr::now, Ui::Text::Bold)
			.append(' ')
			.append(QChar(0x2013)));
	_infoMiddle->setText(row.label);
	_infoRight->setMarkedText(Ui::Text::Colorized(row.info));
	resizeToWidthInfo(width());
	_progress->setValue(row.progress);
}

void TopBar::resizeEvent(QResizeEvent *e) {
	resizeToWidthInfo(e->size().width());
	_button->setGeometry(0, 0, width(), height() - st::lineWidth);
	_progress->setGeometry(
		0,
		height() - st::mediaPlayerPlayback.fullWidth,
		width(),
		st::mediaPlayerPlayback.fullWidth);
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto fill = e->rect().intersected(
		QRect(0, 0, width(), st::mediaPlayerHeight));
	if (!fill.isEmpty()) {
		p.fillRect(fill, st::mediaPlayerBg);
	}
}

void TopBar::setShadowGeometryToLeft(int x, int y, int w, int h) {
	_shadow->setGeometryToLeft(x, y, w, h);
}

void TopBar::showShadow() {
	_shadow->show();
	_progress->show();
}

void TopBar::hideShadow() {
	_shadow->hide();
	_progress->hide();
}

TopBar::~TopBar() = default;

} // namespace View
} // namespace Export

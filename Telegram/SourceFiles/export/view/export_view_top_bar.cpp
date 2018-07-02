/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_top_bar.h"

#include "export/view/export_view_content.h"
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
, _info(this, st::exportTopBarLabel)
, _shadow(this)
, _progress(this, st::mediaPlayerPlayback)
, _button(this) {
	resize(width(), st::mediaPlayerHeight + st::lineWidth);
	_progress->setAttribute(Qt::WA_TransparentForMouseEvents);
	updateData(std::move(content));
}

rpl::producer<> TopBar::clicks() const {
	return _button->clicks();
}

void TopBar::updateData(Content &&content) {
	if (content.rows.empty()) {
		return;
	}
	const auto &row = content.rows[0];
	const auto clean = &TextUtilities::Clean;
	_info->setRichText(textcmdStartSemibold()
		+ clean(lang(lng_export_progress_title))
		+ textcmdStopSemibold()
		+ QString::fromUtf8(" \xe2\x80\x93 ")
		+ clean(row.label)
		+ ' '
		+ textcmdLink(1, clean(row.info)));
	_progress->setValue(row.progress);
}

void TopBar::resizeEvent(QResizeEvent *e) {
	_info->moveToLeft(
		st::mediaPlayerPlayLeft + st::mediaPlayerPadding,
		st::mediaPlayerNameTop - st::mediaPlayerName.style.font->ascent);
	_button->setGeometry(0, 0, width(), height() - st::lineWidth);
	_progress->setGeometry(
		0,
		height() - st::mediaPlayerPlayback.fullWidth,
		width(),
		st::mediaPlayerPlayback.fullWidth);
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);
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

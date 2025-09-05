/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_music_button.h"

#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "styles/style_info.h"

namespace Info::Profile {

MusicButton::MusicButton(
	QWidget *parent,
	MusicButtonData data,
	Fn<void()> handler)
: RippleButton(parent, st::infoMusicButtonRipple)
, _performer(std::make_unique<Ui::FlatLabel>(
	this,
	u"- "_q + data.performer,
	st::infoMusicButtonPerformer))
, _title(std::make_unique<Ui::FlatLabel>(
		this,
		data.title,
		st::infoMusicButtonTitle)) {
	rpl::combine(
		_title->naturalWidthValue(),
		_performer->naturalWidthValue()
	) | rpl::start_with_next([=] {
		resizeToWidth(widthNoMargins());
	}, lifetime());

	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_performer->setAttribute(Qt::WA_TransparentForMouseEvents);

	setClickedCallback(std::move(handler));
}

MusicButton::~MusicButton() = default;

void MusicButton::updateData(MusicButtonData data) {
	_performer->setText(u"- "_q + data.performer);
	_title->setText(data.title);
	resizeToWidth(widthNoMargins());
}

void MusicButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.fillRect(e->rect(), st::windowBgOver);
	paintRipple(p, QPoint());

	auto pen = st::windowBoldFg->p;
	pen.setCapStyle(Qt::RoundCap);
	pen.setWidthF(st::infoMusicButtonLine);
	p.setPen(pen);

	const auto line = st::infoMusicButtonLine;
	const auto length = height() / 4.;
	const auto half = height() / 2.;
	const auto left = st::infoProfileCover.photoLeft + (line / 2.);

	auto hq = PainterHighQualityEnabler(p);
	p.drawLine(
		left, half - length / 2.,
		left, half + length / 2.);
	p.drawLine(
		left + 2.5 * line, half - length,
		left + 2.5 * line, half + length);
	p.drawLine(
		left + 5 * line, half - length * 3 / 4.,
		left + 5 * line, half + length * 3 / 4.);
}

int MusicButton::resizeGetHeight(int newWidth) {
	const auto padding = st::infoMusicButtonPadding;
	const auto &font = st::infoMusicButtonTitle.style.font;

	const auto top = padding.top();
	const auto skip = st::normalFont->spacew;
	const auto available = newWidth - padding.left() - padding.right();
	_title->resizeToNaturalWidth(available);
	_title->moveToLeft(padding.left(), top);
	if (const auto left = available - _title->width() - skip; left > 0) {
		_performer->show();
		_performer->resizeToNaturalWidth(left);
		_performer->moveToLeft(padding.left() + _title->width() + skip, top);
	} else {
		_performer->hide();
	}

	return padding.top() + font->height + padding.bottom();
}

} // namespace Info::Profile

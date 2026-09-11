/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_audio_disc_button.h"

#include "lang/lang_keys.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_editor.h"

#include <QtGui/QContextMenuEvent>

namespace Editor {
namespace {

constexpr auto kSpinDuration = crl::time(4000);

} // namespace

AudioDiscButton::AudioDiscButton(QWidget *parent)
: RippleButton(parent, st::photoEditorRotateButton.ripple) {
	resize(Size(st::photoEditorAudioDiscSize));
	_spin.init([=](crl::time now) {
		update();
		return _active;
	});
}

AudioDiscButton::~AudioDiscButton() = default;

void AudioDiscButton::setCover(const QImage &cover) {
	if (cover.isNull()) {
		_cover = QImage();
	} else {
		const auto ratio = style::DevicePixelRatio();
		const auto side = st::photoEditorAudioDiscCoverSize * ratio;
		auto scaled = cover.scaled(
			side,
			side,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		const auto crop = QRect(
			(scaled.width() - side) / 2,
			(scaled.height() - side) / 2,
			side,
			side);
		_cover = Images::Circle(scaled.copy(crop));
		_cover.setDevicePixelRatio(ratio);
	}
	update();
}

void AudioDiscButton::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_spinBase = angle();
	_active = active;
	_spinStarted = crl::now();
	if (_active) {
		_spin.start();
	} else {
		_spin.stop();
	}
	update();
}

rpl::producer<> AudioDiscButton::removeRequests() const {
	return _removeRequests.events();
}

float64 AudioDiscButton::angle() const {
	if (!_active) {
		return _spinBase;
	}
	const auto passed = crl::now() - _spinStarted;
	return std::fmod(_spinBase + passed * 360. / kSpinDuration, 360.);
}

void AudioDiscButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto full = QRectF(rect());
	p.setPen(Qt::NoPen);
	p.setBrush(st::roundedBg);
	p.drawEllipse(full);
	paintRipple(p, QPoint());
	if (_active) {
		const auto ring = float64(st::photoEditorAudioDiscRing);
		auto pen = QPen(st::photoEditorAudioDiscRingFg->c);
		pen.setWidthF(ring);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(full.marginsRemoved(
			{ ring / 2., ring / 2., ring / 2., ring / 2. }));
		p.setPen(Qt::NoPen);
	}
	if (_cover.isNull()) {
		const auto &icon = isOver()
			? st::photoEditorAudioDiscIconOver
			: st::photoEditorAudioDiscIcon;
		icon.paintInCenter(p, rect());
		return;
	}
	const auto cover = float64(st::photoEditorAudioDiscCoverSize);
	p.save();
	p.translate(rect::center(full));
	p.rotate(angle());
	p.drawImage(QRectF(-cover / 2., -cover / 2., cover, cover), _cover);
	p.restore();
}

void AudioDiscButton::contextMenuEvent(QContextMenuEvent *e) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::photoEditorCropRatioMenu);
	_menu->addAction(tr::lng_photo_editor_audio_remove(tr::now), [=] {
		_removeRequests.fire({});
	}, &st::photoEditorMenuDelete);
	_menu->popup(e->globalPos());
	e->accept();
}

void AudioDiscButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage AudioDiscButton::prepareRippleMask() const {
	return Images::EllipseMask(size());
}

QPoint AudioDiscButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

} // namespace Editor

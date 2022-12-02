/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/loading_element.h"

#include "ui/effects/glare.h"

#include "base/object_ptr.h"
#include "styles/palette.h"
#include "styles/style_basic.h"
#include "styles/style_widgets.h"
#include "base/random.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

namespace Ui {
namespace {

class LoadingElement {
public:
	LoadingElement() = default;

	[[nodiscard]] virtual int height() const = 0;
	virtual void paint(QPainter &p, int width) = 0;
};

class LoadingText final : public LoadingElement {
public:
	LoadingText(const style::FlatLabel &st);

	[[nodiscard]] int height() const override;
	void paint(QPainter &p, int width) override;

private:
	const style::FlatLabel &_st;

};

LoadingText::LoadingText(const style::FlatLabel &st) : _st(st) {
}

int LoadingText::height() const {
	return _st.style.lineHeight;
}

void LoadingText::paint(QPainter &p, int width) {
	auto hq = PainterHighQualityEnabler(p);

	p.setPen(Qt::NoPen);

	p.setBrush(st::dialogsDateFg);
	const auto h = _st.style.font->ascent;
	p.drawRoundedRect(
		0,
		height() - h - (height() - _st.style.font->height),
		width,
		h,
		h / 2,
		h / 2);
}

} // namespace

object_ptr<Ui::RpWidget> CreateLoadingTextWidget(
		not_null<Ui::RpWidget*> parent,
		const style::FlatLabel &st,
		int lines,
		rpl::producer<bool> rtl) {
	auto widget = object_ptr<Ui::RpWidget>(parent);
	const auto raw = widget.data();

	struct State {
		GlareEffect glare;
		Ui::Animations::Simple animation;
		int lastLineWidth = 0;
		rpl::variable<bool> rtl;
	};

	const auto state = widget->lifetime().make_state<State>();
	state->rtl = std::move(rtl);
	state->rtl.value(
	) | rpl::start_with_next([=] { raw->update(); }, raw->lifetime());
	raw->resize(raw->width(), LoadingText(st).height() * lines);

	const auto draw = [=](QPainter &p) {
		auto loading = LoadingText(st);
		const auto countRows = lines;
		for (auto i = 0; i < countRows; i++) {
			const auto w = (i == countRows - 1)
				? state->lastLineWidth
				: raw->width();
			loading.paint(p, w);
			p.translate(0, loading.height());
		}
		p.resetTransform();

		auto &_glare = state->glare;
		if (_glare.glare.birthTime) {
			const auto progress = _glare.progress(crl::now());
			const auto x = (-_glare.width)
				+ (raw->width() + _glare.width * 2) * progress;
			const auto h = raw->height();

			p.drawTiledPixmap(x, 0, _glare.width, h, _glare.pixmap, 0, 0);
		}
	};

	widget->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(raw);
		if (state->rtl.current()) {
			const auto r = raw->rect();
			p.translate(r.center());
			p.scale(-1., 1.);
			p.translate(-r.center());
		}
		draw(p);
	}, widget->lifetime());

	constexpr auto kTimeout = crl::time(1000);
	constexpr auto kDuration = crl::time(1000);
	widget->widthValue(
	) | rpl::start_with_next([=](int width) {
		state->glare.width = width;
		state->glare.validate(
			st::dialogsBg->c,
			[=] { raw->update(); },
			kTimeout,
			kDuration);
		if (width) {
			state->lastLineWidth = (width / 4) + base::RandomIndex(width / 2);
		}
	}, widget->lifetime());

	return widget;
}

} // namespace Ui

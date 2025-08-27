/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/loading_element.h"

#include "base/object_ptr.h"
#include "base/random.h"
#include "styles/palette.h"
#include "ui/effects/glare.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/rp_widget.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

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

	p.setBrush(st::windowBgOver);
	const auto h = _st.style.font->ascent;
	p.drawRoundedRect(
		0,
		height() - h - (height() - _st.style.font->height),
		width,
		h,
		h / 2,
		h / 2);
}

[[nodiscard]] const style::PeerListItem &PeerListItemFromDialogRow(
		rpl::lifetime &lifetime,
		const style::DialogRow &st) {
	using namespace style;
	const auto item = lifetime.make_state<PeerListItem>(PeerListItem{
		.height = st.height,
		.photoPosition = QPoint(st.padding.left(), st.padding.top()),
		.namePosition = QPoint(st.nameLeft, st.nameTop),
		.nameStyle = st::semiboldTextStyle,
		.statusPosition = QPoint(st.textLeft, st.textTop),
		.photoSize = st.photoSize,
	});
	return *item;
}

class LoadingPeerListItem final : public LoadingElement {
public:
	LoadingPeerListItem(const style::PeerListItem &st) : _st(st) {
	}
	LoadingPeerListItem(const style::DialogRow &st)
	: _st(PeerListItemFromDialogRow(_lifetime, st)) {
	}

	[[nodiscard]] int height() const override {
		return _st.height;
	}
	void paint(QPainter &p, int width) override {
		auto hq = PainterHighQualityEnabler(p);

		const auto &style = _st.nameStyle;
		const auto offset = -style.font->ascent
			- (style.lineHeight - style.font->height);

		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);

		p.drawEllipse(
			_st.photoPosition.x(),
			_st.photoPosition.y(),
			_st.photoSize,
			_st.photoSize);

		constexpr auto kNameWidth = 60;
		constexpr auto kStatusWidth = 100;

		const auto h1 = st::semiboldTextStyle.font->ascent;
		p.drawRoundedRect(
			_st.namePosition.x(),
			_st.namePosition.y() + offset,
			kNameWidth,
			h1,
			h1 / 2,
			h1 / 2);

		{
			const auto h2 = st::defaultTextStyle.font->ascent;
			const auto radius = h2 / 2;
			const auto rect = QRect(
				_st.statusPosition.x(),
				_st.statusPosition.y() + offset,
				kStatusWidth,
				h2);
			if (rect::bottom(rect) < height()) {
				p.drawRoundedRect(rect, radius, radius);
			}
		}
	}

private:
	rpl::lifetime _lifetime;
	const style::PeerListItem &_st;

};

template <typename Element, typename ...ElementArgs>
object_ptr<Ui::RpWidget> CreateLoadingElementWidget(
		not_null<Ui::RpWidget*> parent,
		int lines,
		rpl::producer<bool> rtl,
		ElementArgs &&...args) {
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
	raw->resize(
		raw->width(),
		Element(std::forward<ElementArgs>(args)...).height() * lines);

	const auto draw = [=](QPainter &p) {
		auto loading = Element(std::forward<ElementArgs>(args)...);
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

} // namespace

object_ptr<Ui::RpWidget> CreateLoadingTextWidget(
		not_null<Ui::RpWidget*> parent,
		const style::FlatLabel &st,
		int lines,
		rpl::producer<bool> rtl) {
	return CreateLoadingElementWidget<LoadingText>(
		parent,
		lines,
		std::move(rtl),
		st);
}

object_ptr<Ui::RpWidget> CreateLoadingPeerListItemWidget(
		not_null<Ui::RpWidget*> parent,
		const style::PeerListItem &st,
		int lines) {
	return CreateLoadingElementWidget<LoadingPeerListItem>(
		parent,
		lines,
		rpl::single(false),
		st);
}

object_ptr<Ui::RpWidget> CreateLoadingDialogRowWidget(
		not_null<Ui::RpWidget*> parent,
		const style::DialogRow &st,
		int lines) {
	return CreateLoadingElementWidget<LoadingPeerListItem>(
		parent,
		lines,
		rpl::single(false),
		st);
}

} // namespace Ui

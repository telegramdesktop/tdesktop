/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_slider.h"

#include "media/stories/media_stories_controller.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_widgets.h"
#include "styles/style_media_view.h"

namespace Media::Stories {

Slider::Slider(not_null<Controller*> controller)
: _controller(controller) {
}

Slider::~Slider() {
}

void Slider::show(SliderData data) {
	if (_data == data) {
		return;
	}
	_data = data;

	const auto parent = _controller->wrap();
	auto widget = std::make_unique<Ui::RpWidget>(parent);
	const auto raw = widget.get();

	raw->paintRequest(
	) | rpl::filter([=] {
		return (raw->width() >= st::storiesSlider.width);
	}) | rpl::start_with_next([=](QRect clip) {
		auto clipf = QRectF(clip);
		auto p = QPainter(raw);
		const auto single = st::storiesSlider.width;
		const auto skip = st::storiesSliderSkip;
		// width() == single * max + skip * (max - 1);
		// max == (width() + skip) / (single + skip);
		const auto max = (raw->width() + skip) / (single + skip);
		Assert(max > 0);
		const auto count = std::clamp(_data.total, 1, max);
		const auto index = std::clamp(data.index, 0, count - 1);
		const auto radius = st::storiesSlider.width / 2.;
		const auto width = (raw->width() - (count - 1) * skip)
			/ float64(count);
		auto hq = PainterHighQualityEnabler(p);
		auto left = 0.;
		for (auto i = 0; i != count; ++i) {
			const auto rect = QRectF(left, 0, width, single);
			p.setBrush((i == index) // #TODO stories
				? st::mediaviewPipControlsFgOver
				: st::mediaviewPipPlaybackInactive);
			p.setPen(Qt::NoPen);
			p.drawRoundedRect(rect, radius, radius);
			left += width + skip;
		}
	}, raw->lifetime());

	raw->show();
	_widget = std::move(widget);

	_controller->layoutValue(
	) | rpl::start_with_next([=](const Layout &layout) {
		raw->setGeometry(layout.slider - st::storiesSliderMargin);
	}, raw->lifetime());
}

} // namespace Media::Stories

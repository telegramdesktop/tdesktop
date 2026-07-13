/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "poll/poll_link_thumbnail.h"

#include "ui/painter.h"
#include "styles/style_polls.h"

namespace Poll {
namespace {

class LinkThumbnail final : public Ui::DynamicImage {
public:
	std::shared_ptr<Ui::DynamicImage> clone() override {
		return std::make_shared<LinkThumbnail>();
	}
	QImage image(int size) override {
		const auto good = (_frame.width()
			== size * _frame.devicePixelRatio());
		const auto paletteVersion = style::PaletteVersion();
		if (!good || _paletteVersion != paletteVersion) {
			_paletteVersion = paletteVersion;
			const auto ratio = style::DevicePixelRatio();
			if (!good) {
				_frame = QImage(
					QSize(size, size) * ratio,
					QImage::Format_ARGB32_Premultiplied);
				_frame.setDevicePixelRatio(ratio);
			}
			_frame.fill(Qt::transparent);
			auto p = Painter(&_frame);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			const auto radius = size / 4.;
			p.drawRoundedRect(QRect(0, 0, size, size), radius, radius);
			st::historyPollLinkPlaceholderIcon.paintInCenter(
				p,
				QRect(0, 0, size, size));
		}
		return _frame;
	}
	void subscribeToUpdates(Fn<void()> callback) override {
		if (!callback) {
			_frame = {};
		}
	}

private:
	int _paletteVersion = 0;
	QImage _frame;

};

} // namespace

std::shared_ptr<Ui::DynamicImage> MakeLinkThumbnail() {
	return std::make_shared<LinkThumbnail>();
}

} // namespace Poll

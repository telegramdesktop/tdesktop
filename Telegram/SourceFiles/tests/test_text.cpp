/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tests/test_main.h"

#include "base/invoke_queued.h"
#include "base/integration.h"
#include "ui/effects/animations.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/rp_window.h"
#include "ui/painter.h"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QThread>
#include <QDir>

namespace Test {

QString name() {
	return u"text"_q;
}

void test(not_null<Ui::RpWindow*> window, not_null<Ui::RpWidget*> body) {
	auto text = new Ui::Text::String(scale(64));

	const auto like = QString::fromUtf8("\xf0\x9f\x91\x8d");
	const auto dislike = QString::fromUtf8("\xf0\x9f\x91\x8e");
	const auto hebrew = QString() + QChar(1506) + QChar(1460) + QChar(1489);

	auto data = TextWithEntities();
	data.append(
		u"Lorem ipsu7m dolor sit amet, "_q
	).append(Ui::Text::Bold(
		u"consectetur adipiscing: "_q
		+ hebrew
		+ u" elit, sed do eiusmod tempor incididunt test"_q
	)).append(Ui::Text::Wrapped(Ui::Text::Bold(
		u". ut labore et dolore magna aliqua."_q
		+ like
		+ dislike
		+ u"Ut enim ad minim veniam"_q
	), EntityType::Italic)).append(
		u", quis nostrud exercitation ullamco laboris nisi ut aliquip ex \
ea commodo consequat. Duis aute irure dolor in reprehenderit in \
voluptate velit esse cillum dolore eu fugiat nulla pariatur. \
Excepteur sint occaecat cupidatat non proident, sunt in culpa \
qui officia deserunt mollit anim id est laborum."_q
).append(u"\n\n"_q).append(hebrew).append("\n\n").append(
		"Duisauteiruredolorinreprehenderitinvoluptatevelitessecillumdoloreeu\
fugiatnullapariaturExcepteursintoccaecatcupidatatnonproident, sunt in culpa \
qui officia deserunt mollit anim id est laborum. \
Duisauteiruredolorinreprehenderitinvoluptate.");
	data.append(data);
	//data.append("hi\n\nguys");
	text->setMarkedText(st::defaultTextStyle, data);

	body->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(body);
		auto hq = PainterHighQualityEnabler(p);

		const auto width = body->width();
		const auto height = body->height();

		p.fillRect(clip, QColor(255, 255, 255));

		const auto border = QColor(0, 128, 0, 16);
		auto skip = scale(20);
		p.fillRect(0, 0, skip, height, border);
		p.fillRect(skip, 0, width - skip, skip, border);
		p.fillRect(skip, height - skip, width - skip, skip, border);
		p.fillRect(width - skip, skip, skip, height - skip * 2, border);

		const auto inner = body->rect().marginsRemoved(
			{ skip, skip, skip, skip });

		p.fillRect(QRect{
			inner.x(),
			inner.y(),
			inner.width(),
			text->countHeight(inner.width())
		}, QColor(128, 0, 0, 16));

		auto widths = text->countLineWidths(inner.width());
		auto top = 0;
		for (const auto width : widths) {
			p.fillRect(QRect{
				inner.x(),
				inner.y() + top,
				width,
				st::defaultTextStyle.font->height
			}, QColor(0, 0, 128, 16));
			top += st::defaultTextStyle.font->height;
		}

		text->draw(p, {
			.position = inner.topLeft(),
			.availableWidth = inner.width(),
		});

		//const auto to = QRectF(
		//	inner.marginsRemoved({ 0, inner.height() / 2, 0, 0 }));
		//const auto t = u"hi\n\nguys"_q;
		//p.drawText(to, t);
	}, body->lifetime());
}

} // namespace Test

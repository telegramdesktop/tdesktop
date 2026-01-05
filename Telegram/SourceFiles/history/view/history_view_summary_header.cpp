/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_summary_header.h"

#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/data_session.h"
#include "history/history_item_components.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/about_cocoon_box.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {

SummaryHeader::SummaryHeader()
: _name(st::maxSignatureSize / 2)
, _text(st::maxSignatureSize / 2) {
}

SummaryHeader &SummaryHeader::operator=(SummaryHeader &&other) = default;

SummaryHeader::~SummaryHeader() = default;

void SummaryHeader::update(not_null<Element*> view) {
	const auto item = view->data();

	using namespace Ui;
	_animation = std::make_unique<Animation>(Animation{
		.particles = StarParticles(
			StarParticles::Type::Right,
			15,
			st::lineWidth * 8),
	});
	_animation->particles.setSpeed(0.05);

	_text.setText(
		st::defaultTextStyle,
		tr::lng_summarize_header_about(tr::now));

	_name.setText(
		st::msgNameStyle,
		tr::lng_summarize_header_title(tr::now));

	_maxWidth = st::historyReplyPadding.left()
		+ st::maxSignatureSize / 2
		+ st::historyReplyPadding.right();

	_lottie = Lottie::MakeIcon(Lottie::IconDescriptor{
		.name = u"cocoon"_q,
		.sizeOverride = Size(st::historySummaryHeaderIconSize
			- st::historySummaryHeaderIconSizeInner * 2),
	});

	const auto session = &item->history()->session();
	const auto itemId = item->fullId();
	_link = std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		if (Rect(iconRect().size()).contains(_iconRipple.lastPoint)) {
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto controller = my.sessionWindow.get()) {
				controller->show(Box(Ui::AboutCocoonBox));
				return;
			}
		}
		if (const auto item = session->data().message(itemId)) {
			session->api().transcribes().toggleSummary(item, nullptr);
		}
	});
}

bool SummaryHeader::isNameUpdated(not_null<const Element*> view) const {
	return false;
}

int SummaryHeader::resizeToWidth(int width) const {
	_ripple.animation = nullptr;
	_height = st::historyReplyPadding.top()
		+ st::msgServiceNameFont->height * 2
		+ st::historyReplyPadding.bottom();
	_width = width;
	return _height;
}

int SummaryHeader::height() const {
	return _height + st::historyReplyTop + st::historyReplyBottom;
}

QMargins SummaryHeader::margins() const {
	return QMargins(0, st::historyReplyTop, 0, st::historyReplyBottom);
}

void SummaryHeader::paint(
		Painter &p,
		not_null<const Element*> view,
		const Ui::ChatPaintContext &context,
		int x,
		int y,
		int w,
		bool inBubble) const {
	const auto st = context.st;
	const auto stm = context.messageStyle();

	y += st::historyReplyTop;
	const auto rect = QRect(x, y, w, _height);
	const auto colorPattern = 0;
	const auto cache = !inBubble
		? st->serviceReplyCache(colorPattern).get()
		: stm->replyCache[colorPattern].get();
	const auto &quoteSt = st::messageQuoteStyle;
	const auto rippleColor = cache->bg;
	if (!inBubble) {
		cache->bg = QColor(0, 0, 0, 0);
	}
	Ui::Text::ValidateQuotePaintCache(*cache, quoteSt);
	Ui::Text::FillQuotePaint(p, rect, *cache, quoteSt);
	if (!inBubble) {
		cache->bg = rippleColor;
	}

	if (_lottie) {
		const auto r = iconRect().translated(x, y);
		const auto lottieX = r.x() + st::historySummaryHeaderIconSizeInner;
		const auto lottieY = r.y() + st::historySummaryHeaderIconSizeInner;
		_lottie->paint(p, lottieX, lottieY);
		if (_iconRipple.animation) {
			_iconRipple.animation->paint(
				p,
				r.x(),
				r.y(),
				r.width(),
				&rippleColor);
			if (_iconRipple.animation->empty()) {
				_iconRipple.animation.reset();
			}
		}
	}

	if (_animation) {
		const auto size = QSize(w, _height);
		if (_animation->cachedSize != size) {
			_animation->path = QPainterPath();
			_animation->path.addRoundedRect(
				QRect(0, 0, w, _height),
				quoteSt.radius,
				quoteSt.radius);
			_animation->cachedSize = size;
		}
		p.translate(x, y);
		p.setClipPath(_animation->path);
		const auto nameColor = !inBubble
			? st->msgImgReplyBarColor()->c
			: stm->msgServiceFg->c;
		_animation->particles.setColor(nameColor);
		const auto paused = context.paused || On(PowerSaving::kChatEffects);
		_animation->particles.paint(
			p,
			QRect(0, 0, w, _height),
			context.now,
			paused);
		if (!paused) {
			const auto session = &view->history()->session();
			const auto r = QRect(x, y, w, _height);
			Ui::PostponeCall(session, [=, itemId = view->data()->fullId()] {
				if (const auto i = session->data().message(itemId)) {
					session->data().requestItemRepaint(i, r);
				}
			});
		}
		p.setClipping(false);
		p.translate(-x, -y);
	}

	auto textLeft = x + st::historyReplyPadding.left();
	auto textTop = y + st::historyReplyPadding.top()
		+ st::msgServiceNameFont->height;
	if (w > st::historyReplyPadding.left()) {
		const auto iconSpace = st::historySummaryHeaderIconSize
			+ st::historySummaryHeaderIconSizeInner * 2;
		const auto textw = w
			- st::historyReplyPadding.left()
			- st::historyReplyPadding.right()
			- iconSpace;
		const auto namew = textw;
		if (namew > 0) {
			p.setPen(!inBubble
				? st->msgImgReplyBarColor()->c
				: stm->msgServiceFg->c);
			_name.drawLeftElided(
				p,
				x + st::historyReplyPadding.left(),
				y + st::historyReplyPadding.top(),
				namew,
				w + 2 * x,
				1);

			p.setPen(inBubble
				? stm->historyTextFg
				: st->msgImgReplyBarColor());
			view->prepareCustomEmojiPaint(p, context, _text);
			auto replyToTextPalette = &(!inBubble
				? st->imgReplyTextPalette()
				: stm->replyTextPalette);
			_text.draw(p, {
				.position = { textLeft, textTop },
				.availableWidth = textw,
				.palette = replyToTextPalette,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.now = context.now,
				.pausedEmoji = (context.paused
					|| On(PowerSaving::kEmojiChat)),
				.pausedSpoiler = (context.paused
					|| On(PowerSaving::kChatSpoiler)),
				.elisionLines = 1,
			});
			p.setTextPalette(stm->textPalette);
		}
	}

	if (_ripple.animation) {
		_ripple.animation->paint(p, x, y, w, &rippleColor);
		if (_ripple.animation->empty()) {
			_ripple.animation.reset();
		}
	}
}

void SummaryHeader::createRippleAnimation(
		not_null<const Element*> view,
		QSize size) {
	_ripple.animation = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		Ui::RippleAnimation::RoundRectMask(
			size,
			st::messageQuoteStyle.radius),
		[=] { view->repaint(); });
	const auto rippleIconSize = st::historySummaryHeaderIconSize;
	_iconRipple.animation = std::make_unique<Ui::RippleAnimation>(
		st::defaultRippleAnimation,
		Ui::RippleAnimation::EllipseMask(Size(rippleIconSize)),
		[=] { view->repaint(); });
}

void SummaryHeader::saveRipplePoint(QPoint point) const {
	_ripple.lastPoint = point;
	const auto rect = iconRect();
	if (rect.contains(point)) {
		_iconRipple.lastPoint = point - rect.topLeft();
	} else {
		_iconRipple.lastPoint = QPoint(-1, -1);
	}
}

void SummaryHeader::addRipple() {
	if (_iconRipple.lastPoint.x() >= 0 && _iconRipple.animation) {
		_iconRipple.animation->add(_iconRipple.lastPoint);
	} else if (_ripple.animation) {
		_ripple.animation->add(_ripple.lastPoint);
	}
}

void SummaryHeader::stopLastRipple() {
	if (_ripple.animation) {
		_ripple.animation->lastStop();
	}
	if (_iconRipple.animation) {
		_iconRipple.animation->lastStop();
	}
}

void SummaryHeader::unloadHeavyPart() {
	_animation = nullptr;
	_ripple.animation = nullptr;
	_iconRipple.animation = nullptr;
	_lottie = nullptr;
}

QRect SummaryHeader::iconRect() const {
	const auto size = st::historySummaryHeaderIconSize;
	const auto shift = st::historySummaryHeaderIconSizeInner;
	return QRect(_width - size - shift, (_height - size) / 2, size, size);
}

} // namespace HistoryView

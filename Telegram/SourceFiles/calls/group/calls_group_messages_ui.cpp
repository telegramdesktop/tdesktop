/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_messages_ui.h"

#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/premium_preview_box.h"
#include "calls/group/calls_group_messages.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_message_reactions.h"
#include "data/data_message_reaction_id.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "styles/style_calls.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_media_view.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

namespace Calls::Group {
namespace {

constexpr auto kMessageBgOpacity = 0.8;

[[nodiscard]] int CountMessageRadius() {
	const auto minHeight = st::groupCallMessagePadding.top()
		+ st::messageTextStyle.font->height
		+ st::groupCallMessagePadding.bottom();
	return minHeight / 2;
}

void ReceiveSomeMouseEvents(
		not_null<Ui::ElasticScroll*> scroll,
		Fn<bool(QPoint)> handleClick) {
	class EventFilter final : public QObject {
	public:
		explicit EventFilter(
			not_null<Ui::ElasticScroll*> scroll,
			Fn<bool(QPoint)> handleClick)
		: QObject(scroll)
		, _handleClick(std::move(handleClick)) {
		}

		bool eventFilter(QObject *watched, QEvent *event) {
			if (event->type() == QEvent::MouseButtonPress) {
				return mousePressFilter(
					watched,
					static_cast<QMouseEvent*>(event));
			} else if (event->type() == QEvent::Wheel) {
				return wheelFilter(
					watched,
					static_cast<QWheelEvent*>(event));
			}
			return false;
		}

		bool mousePressFilter(
				QObject *watched,
				not_null<QMouseEvent*> event) {
			Expects(parent()->isWidgetType());

			const auto scroll = static_cast<Ui::ElasticScroll*>(parent());
			if (watched != scroll->window()->windowHandle()) {
				return false;
			}
			const auto global = event->globalPos();
			const auto local = scroll->mapFromGlobal(global);
			if (!scroll->rect().contains(local)) {
				return false;
			}
			return _handleClick(local + QPoint(0, scroll->scrollTop()));
		}

		bool wheelFilter(QObject *watched, not_null<QWheelEvent*> event) {
			Expects(parent()->isWidgetType());

			const auto scroll = static_cast<Ui::ElasticScroll*>(parent());
			if (watched != scroll->window()->windowHandle()
				|| !scroll->scrollTopMax()) {
				return false;
			}
			const auto global = event->globalPosition().toPoint();
			const auto local = scroll->mapFromGlobal(global);
			if (!scroll->rect().contains(local)) {
				return false;
			}
			auto e = QWheelEvent(
				event->position(),
				event->globalPosition(),
				event->pixelDelta(),
				event->angleDelta(),
				event->buttons(),
				event->modifiers(),
				event->phase(),
				event->inverted(),
				event->source());
			e.setTimestamp(crl::now());
			QGuiApplication::sendEvent(scroll, &e);
			return true;
		}

	private:
		Fn<bool(QPoint)> _handleClick;

	};

	scroll->setAttribute(Qt::WA_TransparentForMouseEvents);
	qApp->installEventFilter(
		new EventFilter(scroll, std::move(handleClick)));
}

} // namespace

struct MessagesUi::MessageView {
	uint64 id = 0;
	PeerData *from = nullptr;
	ClickHandlerPtr fromLink;
	Ui::Animations::Simple toggleAnimation;
	Ui::Animations::Simple sentAnimation;
	Data::ReactionId reactionId;
	std::unique_ptr<Ui::InfiniteRadialAnimation> sendingAnimation;
	std::unique_ptr<Ui::ReactionFlyAnimation> reactionAnimation;
	std::unique_ptr<Ui::RpWidget> reactionWidget;
	QPoint reactionShift;
	Ui::PeerUserpicView view;
	Ui::Text::String text;
	int top = 0;
	int width = 0;
	int left = 0;
	int height = 0;
	int realHeight = 0;
	bool removed = false;
	bool sending = false;
	bool failed = false;
};

MessagesUi::MessagesUi(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	rpl::producer<std::vector<Message>> messages,
	rpl::producer<bool> shown)
: _parent(parent)
, _show(std::move(show))
, _messageBg([] {
	auto result = st::groupCallBg->c;
	result.setAlphaF(kMessageBgOpacity);
	return result;
})
, _messageBgRect(CountMessageRadius(), _messageBg.color())
, _fadeHeight(st::normalFont->height) {
	setupList(std::move(messages), std::move(shown));
}

MessagesUi::~MessagesUi() = default;

void MessagesUi::setupList(
		rpl::producer<std::vector<Message>> messages,
		rpl::producer<bool> shown) {
	rpl::combine(
		std::move(messages),
		std::move(shown)
	) | rpl::start_with_next([=](std::vector<Message> &&list, bool shown) {
		if (!shown) {
			list.clear();
		}
		auto from = begin(list);
		auto till = end(list);
		for (auto &entry : _views) {
			if (!entry.removed) {
				const auto id = entry.id;
				const auto i = ranges::find(
					from,
					till,
					id,
					&Message::randomId);
				if (i == till) {
					toggleMessage(entry, false);
					continue;
				} else if (entry.failed != i->failed) {
					setContentFailed(entry);
					updateMessageSize(entry);
					repaintMessage(entry.id);
				} else if (entry.sending != (i->date == 0)) {
					animateMessageSent(entry);
				}
				if (i == from) {
					++from;
				}
			}
		}
		auto addedSendingToBottom = false;
		for (auto i = from; i != till; ++i) {
			if (!ranges::contains(_views, i->randomId, &MessageView::id)) {
				if (i + 1 == till && !i->date) {
					addedSendingToBottom = true;
				}
				appendMessage(*i);
			}
		}
		if (addedSendingToBottom) {
			const auto from = _scroll->scrollTop();
			const auto till = _scroll->scrollTopMax();
			_scrollToBottomAnimation.start([=] {
				_scroll->scrollToY(_scrollToBottomAnimation.value(till));
			}, from, till, st::slideDuration, anim::easeOutCirc);
		}
	}, _lifetime);
}

void MessagesUi::animateMessageSent(MessageView &entry) {
	const auto id = entry.id;
	entry.sending = false;
	entry.sentAnimation.start([=] {
		repaintMessage(id);
	}, 0., 1, st::slideDuration, anim::easeOutCirc);
	repaintMessage(id);
}

void MessagesUi::updateMessageSize(MessageView &entry) {
	const auto &padding = st::groupCallMessagePadding;

	const auto hasUserpic = !entry.failed;
	const auto userpicPadding = st::groupCallUserpicPadding;
	const auto userpicSize = st::groupCallUserpic;
	const auto leftSkip = hasUserpic
		? (userpicPadding.left() + userpicSize + userpicPadding.right())
		: padding.left();
	const auto widthSkip = leftSkip + padding.right();
	const auto inner = _width - widthSkip;

	const auto size = Ui::Text::CountOptimalTextSize(
		entry.text,
		std::min(st::groupCallWidth / 2, inner),
		inner);

	const auto textHeight = size.height();
	entry.width = size.width() + widthSkip;
	entry.left = (_width - entry.width) / 2;
	updateReactionPosition(entry);

	const auto contentHeight = padding.top() + textHeight + padding.bottom();
	const auto userpicHeight = hasUserpic
		? (userpicPadding.top() + userpicSize + userpicPadding.bottom())
		: 0;

	const auto skip = st::groupCallMessageSkip;
	entry.realHeight = skip + std::max(contentHeight, userpicHeight);
}

bool MessagesUi::updateMessageHeight(MessageView &entry) {
	const auto height = entry.toggleAnimation.animating()
		? anim::interpolate(
			0,
			entry.realHeight,
			entry.toggleAnimation.value(entry.removed ? 0. : 1.))
		: entry.realHeight;
	if (entry.height == height) {
		return false;
	}
	entry.height = height;
	return true;
}

void MessagesUi::setContentFailed(MessageView &entry) {
	entry.failed = true;
	entry.text = Ui::Text::String(
		st::messageTextStyle,
		TextWithEntities().append(
			QString::fromUtf8("\xe2\x9d\x97\xef\xb8\x8f")
		).append(' ').append(
			Ui::Text::Italic(u"Failed to send the message."_q)),
		kMarkupTextOptions,
		st::groupCallWidth / 4);
}

void MessagesUi::setContent(
		MessageView &entry,
		const TextWithEntities &text) {
	entry.text = Ui::Text::String(
		st::messageTextStyle,
		text,
		kMarkupTextOptions,
		st::groupCallWidth / 4,
		Core::TextContext({
			.session = &_show->session(),
			.repaint = [this, id = entry.id] { repaintMessage(id); },
		}));
	entry.text.setLink(1, entry.fromLink);
	if (entry.text.hasSpoilers()) {
		const auto id = entry.id;
		const auto guard = base::make_weak(_messages);
		entry.text.setSpoilerLinkFilter([=](const ClickContext &context) {
			if (context.button != Qt::LeftButton || !guard) {
				return false;
			}
			const auto i = ranges::find(
				_views,
				_revealedSpoilerId,
				&MessageView::id);
			if (i != end(_views) && _revealedSpoilerId != id) {
				i->text.setSpoilerRevealed(false, anim::type::normal);
			}
			_revealedSpoilerId = id;
			return true;
		});
	}
}

void MessagesUi::toggleMessage(MessageView &entry, bool shown) {
	const auto id = entry.id;
	entry.removed = !shown;
	entry.toggleAnimation.start(
		[=] { repaintMessage(id); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration,
		shown ? anim::easeOutCirc : anim::easeInCirc);
	repaintMessage(id);
}

void MessagesUi::repaintMessage(uint64 id) {
	auto i = ranges::find(_views, id, &MessageView::id);
	if (i == end(_views)) {
		return;
	} else if (i->removed && !i->toggleAnimation.animating()) {
		const auto top = i->top;
		i = _views.erase(i);
		recountHeights(i, top);
		return;
	}
	if (!i->sending && !i->sentAnimation.animating()) {
		i->sendingAnimation = nullptr;
	}
	if (i->toggleAnimation.animating() || i->height != i->realHeight) {
		if (updateMessageHeight(*i)) {
			recountHeights(i, i->top);
			return;
		}
	}
	_messages->update(0, i->top, _messages->width(), i->height);
}

void MessagesUi::recountHeights(
		std::vector<MessageView>::iterator i,
		int top) {
	auto from = top;
	for (auto e = end(_views); i != e; ++i) {
		i->top = top;
		top += i->height;
		updateReactionPosition(*i);
	}
	if (_views.empty()) {
		delete base::take(_messages);
		_scroll = nullptr;
	} else {
		updateGeometries();
		_messages->update(0, from, _messages->width(), top - from);
	}
}

void MessagesUi::appendMessage(const Message &data) {
	const auto top = _views.empty()
		? 0
		: (_views.back().top + _views.back().height);

	if (!_scroll) {
		setupMessagesWidget();
	}

	auto &entry = _views.emplace_back();
	const auto id = entry.id = data.randomId;
	const auto repaint = [=] {
		repaintMessage(id);
	};
	const auto peer = entry.from = data.peer;
	entry.fromLink = std::make_shared<LambdaClickHandler>([=] {
		_show->show(
			PrepareShortInfoBox(peer, _show, &st::storiesShortInfoBox));
	});
	entry.sending = !data.date;
	if (data.failed) {
		setContentFailed(entry);
	} else {
		setContent(
			entry,
			Ui::Text::Link(Ui::Text::Bold(data.peer->shortName()), 1).append(
				' ').append(data.text));
	}
	entry.top = top;
	updateMessageSize(entry);
	if (entry.sending) {
		using namespace Ui;
		const auto &st = st::defaultInfiniteRadialAnimation;
		entry.sendingAnimation = std::make_unique<InfiniteRadialAnimation>(
			repaint,
			st);
		entry.sendingAnimation->start(0);
	}
	entry.height = 0;
	toggleMessage(entry, true);
	checkReactionContent(entry, data.text);
}

void MessagesUi::checkReactionContent(
		MessageView &entry,
		const TextWithEntities &text) {
	auto outLength = 0;
	using Type = Data::Reactions::Type;
	const auto reactions = &_show->session().data().reactions();
	const auto set = [&](Data::ReactionId id) {
		reactions->preloadAnimationsFor(id);
		entry.reactionId = std::move(id);
	};
	if (text.entities.size() == 1
		&& text.entities.front().type() == EntityType::CustomEmoji
		&& text.entities.front().offset() == 0
		&& text.entities.front().length() == text.text.size()) {
		set({ text.entities.front().data().toULongLong() });
	} else if (const auto emoji = Ui::Emoji::Find(text.text, &outLength)) {
		if (outLength < text.text.size()) {
			return;
		}
		const auto &all = reactions->list(Type::All);
		for (const auto &reaction : all) {
			if (reaction.id.custom()) {
				continue;
			}
			const auto &text = reaction.id.emoji();
			if (Ui::Emoji::Find(text) != emoji) {
				continue;
			}
			set(reaction.id);
			break;
		}
	}
}

void MessagesUi::startReactionAnimation(MessageView &entry) {
	entry.reactionWidget = std::make_unique<Ui::RpWidget>(_parent);
	const auto raw = entry.reactionWidget.get();
	raw->show();
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);

	if (!_effectsLifetime) {
		rpl::combine(
			_scroll->scrollTopValue(),
			_scroll->RpWidget::positionValue()
		) | rpl::start_with_next([=](int yshift, QPoint point) {
			_reactionBasePosition = point - QPoint(0, yshift);
			for (auto &view : _views) {
				updateReactionPosition(view);
			}
		}, _effectsLifetime);
	}

	entry.reactionAnimation = std::make_unique<Ui::ReactionFlyAnimation>(
		&_show->session().data().reactions(),
		Ui::ReactionFlyAnimationArgs{
			.id = entry.reactionId,
			.effectOnly = true,
		},
		[=] { raw->update(); },
		st::reactionInlineImage);
	updateReactionPosition(entry);

	const auto effectSize = st::reactionInlineImage * 2;
	const auto animation = entry.reactionAnimation.get();
	raw->resize(effectSize, effectSize);
	raw->paintRequest() | rpl::start_with_next([=] {
		if (animation->finished()) {
			crl::on_main(raw, [=] {
				removeReaction(raw);
			});
			return;
		}
		auto p = QPainter(raw);
		const auto size = raw->width();
		const auto skip = (size - st::reactionInlineImage) / 2;
		const auto target = QRect(
			QPoint(skip, skip),
			QSize(st::reactionInlineImage, st::reactionInlineImage));
		animation->paintGetArea(
			p,
			QPoint(),
			target,
			st::radialFg->c,
			QRect(),
			crl::now());
	}, raw->lifetime());
}

void MessagesUi::removeReaction(not_null<Ui::RpWidget*> widget) {
	const auto i = ranges::find_if(_views, [&](const MessageView &entry) {
		return entry.reactionWidget.get() == widget;
	});
	if (i != end(_views)) {
		i->reactionId = {};
		i->reactionWidget = nullptr;
		i->reactionAnimation = nullptr;
	};
}

void MessagesUi::updateReactionPosition(MessageView &entry) {
	if (const auto widget = entry.reactionWidget.get()) {
		if (entry.failed) {
			widget->resize(0, 0);
			return;
		}
		const auto padding = st::groupCallMessagePadding;
		const auto userpicSize = st::groupCallUserpic;
		const auto userpicPadding = st::groupCallUserpicPadding;
		const auto esize = st::emojiSize;
		const auto eleft = entry.text.maxWidth() - st::emojiPadding - esize;
		const auto etop = (st::normalFont->height - esize) / 2;
		const auto effectSize = st::reactionInlineImage * 2;
		entry.reactionShift = QPoint(entry.left, entry.top)
			+ QPoint(
				userpicPadding.left() + userpicSize + userpicPadding.right(),
				padding.top())
			+ QPoint(eleft + (esize / 2), etop + (esize / 2))
			- QPoint(effectSize / 2, effectSize / 2);
		widget->move(_reactionBasePosition + entry.reactionShift);
	}
}

void MessagesUi::updateTopFade() {
	const auto topFadeShown = (_scroll->scrollTop() > 0);
	if (_topFadeShown != topFadeShown) {
		_topFadeShown = topFadeShown;
		//const auto from = topFadeShown ? 0. : 1.;
		//const auto till = topFadeShown ? 1. : 0.;
		//_topFadeAnimation.start([=] {
			_messages->update(
				0,
				_scroll->scrollTop(),
				_messages->width(),
				_fadeHeight);
		//}, from, till, st::slideWrapDuration);
	}
}

void MessagesUi::updateBottomFade() {
	const auto max = _scroll->scrollTopMax();
	const auto bottomFadeShown = (_scroll->scrollTop() < max);
	if (_bottomFadeShown != bottomFadeShown) {
		_bottomFadeShown = bottomFadeShown;
		//const auto from = bottomFadeShown ? 0. : 1.;
		//const auto till = bottomFadeShown ? 1. : 0.;
		//_bottomFadeAnimation.start([=] {
			_messages->update(
				0,
				_scroll->scrollTop() + _scroll->height() - _fadeHeight,
				_messages->width(),
				_fadeHeight);
		//}, from, till, st::slideWrapDuration);
	}
}

void MessagesUi::setupMessagesWidget() {
	_scroll = std::make_unique<Ui::ElasticScroll>(
		_parent,
		st::groupCallMessagesScroll);
	const auto scroll = _scroll.get();

	_messages = scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(scroll));
	rpl::combine(
		scroll->scrollTopValue(),
		scroll->heightValue(),
		_messages->heightValue()
	) | rpl::start_with_next([=] {
		updateTopFade();
		updateBottomFade();
	}, scroll->lifetime());

	ReceiveSomeMouseEvents(scroll, [=](QPoint point) {
		for (const auto &entry : _views) {
			if (entry.failed || entry.top + entry.height <= point.y()) {
				continue;
			} else if (entry.top >= point.y()
				|| entry.left >= point.x()
				|| entry.left + entry.width <= point.x()) {
				return false;
			}

			const auto padding = st::groupCallMessagePadding;
			const auto userpicSize = st::groupCallUserpic;
			const auto userpicPadding = st::groupCallUserpicPadding;
			const auto leftSkip = userpicPadding.left()
				+ userpicSize
				+ userpicPadding.right();
			const auto userpic = QRect(
				entry.left + userpicPadding.left(),
				entry.top + userpicPadding.top(),
				userpicSize,
				userpicSize);
			const auto link = userpic.contains(point)
				? entry.fromLink
				: entry.text.getState(point - QPoint(
					entry.left + leftSkip,
					entry.top + padding.top()
				), entry.width - leftSkip - padding.right()).link;
			if (link) {
				ActivateClickHandler(_messages, link, Qt::LeftButton);
			}
			return true;
		}
		return false;
	});

	_messages->paintRequest() | rpl::start_with_next([=](QRect clip) {
		const auto start = scroll->scrollTop();
		const auto end = start + scroll->height();
		const auto ratio = style::DevicePixelRatio();

		if ((_canvas.width() < scroll->width() * ratio)
			|| (_canvas.height() < scroll->height() * ratio)) {
			_canvas = QImage(
				scroll->size() * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_canvas.setDevicePixelRatio(ratio);
		}
		auto p = Painter(&_canvas);

		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.fillRect(QRect(QPoint(), scroll->size()), QColor(0, 0, 0, 0));

		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		const auto now = crl::now();
		const auto skip = st::groupCallMessageSkip;
		const auto padding = st::groupCallMessagePadding;
		p.translate(0, -start);
		for (auto &entry : _views) {
			if (entry.height <= skip || entry.top + entry.height <= start) {
				continue;
			} else if (entry.top >= end) {
				break;
			}
			const auto use = entry.realHeight - skip;
			const auto width = entry.width;
			p.setBrush(st::radialBg);
			p.setPen(Qt::NoPen);
			const auto scaled = (entry.height < entry.realHeight);
			if (scaled) {
				const auto used = entry.height - skip;
				const auto mx = scaled ? (entry.left + (width / 2)) : 0;
				const auto my = scaled ? (entry.top + (used / 2)) : 0;
				const auto scale = used / float64(use);
				p.save();
				p.translate(mx, my);
				p.scale(scale, scale);
				p.setOpacity(scale);
				p.translate(-mx, -my);
			}
			_messageBgRect.paint(p, { entry.left, entry.top, width, use });

			auto leftSkip = padding.left();
			const auto hasUserpic = !entry.failed;
			if (hasUserpic) {
				const auto userpicSize = st::groupCallUserpic;
				const auto userpicPadding = st::groupCallUserpicPadding;
				const auto position = QPoint(
					entry.left + userpicPadding.left(),
					entry.top + userpicPadding.top());
				const auto rect = QRect(
					position,
					QSize(userpicSize, userpicSize));
				entry.from->paintUserpic(p, entry.view, {
					.position = position,
					.size = userpicSize,
					.shape = Ui::PeerUserpicShape::Circle,
				});
				if (const auto animation = entry.sendingAnimation.get()) {
					auto hq = PainterHighQualityEnabler(p);
					auto pen = st::groupCallBg->p;
					const auto shift = userpicPadding.left();
					pen.setWidthF(shift);
					p.setPen(pen);
					p.setBrush(Qt::NoBrush);
					const auto state = animation->computeState();
					const auto sent = entry.sending
						? 0.
						: entry.sentAnimation.value(1.);
					p.setOpacity(state.shown * (1. - sent));
					p.drawArc(
						rect.marginsRemoved({ shift, shift, shift, shift }),
						state.arcFrom,
						state.arcLength);
					p.setOpacity(1.);
				}
				leftSkip = userpicPadding.left()
					+ userpicSize
					+ userpicPadding.right();
			}

			p.setPen(st::radialFg);
			entry.text.draw(p, {
				.position = {
					entry.left + leftSkip,
					entry.top + padding.top()
				},
				.availableWidth = entry.width - leftSkip - padding.right(),
				.palette = &st::groupCallMessagePalette,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.now = now,
				.paused = !_messages->window()->isActiveWindow(),
			});
			if (!scaled && entry.reactionId && !entry.reactionAnimation) {
				startReactionAnimation(entry);
			}

			p.restore();
		}
		p.translate(0, start);

		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.setPen(Qt::NoPen);

		const auto topFade = _topFadeAnimation.value(
			_topFadeShown ? 1. : 0.);
		if (topFade) {
			auto gradientTop = QLinearGradient(0, 0, 0, _fadeHeight);
			gradientTop.setStops({
				{ 0., QColor(255, 255, 255, 0) },
				{ 1., QColor(255, 255, 255, 255) },
			});
			p.setOpacity(topFade);
			p.setBrush(gradientTop);
			p.drawRect(0, 0, scroll->width(), _fadeHeight);
			p.setOpacity(1.);
		}
		const auto bottomFade = _bottomFadeAnimation.value(
			_bottomFadeShown ? 1. : 0.);
		if (bottomFade) {
			const auto till = scroll->height();
			const auto from = till - _fadeHeight;
			auto gradientBottom = QLinearGradient(0, from, 0, till);
			gradientBottom.setStops({
				{ 0., QColor(255, 255, 255, 255) },
				{ 1., QColor(255, 255, 255, 0) },
			});
			p.setBrush(gradientBottom);
			p.drawRect(0, from, scroll->width(), _fadeHeight);
		}
		QPainter(_messages).drawImage(
			QRect(QPoint(0, start), scroll->size()),
			_canvas,
			QRect(QPoint(), scroll->size() * ratio));
	}, _lifetime);

	scroll->show();
	applyWidth();
}

void MessagesUi::applyWidth() {
	if (!_scroll || _width < st::groupCallWidth * 2 / 3) {
		return;
	}
	auto top = 0;
	for (auto &entry : _views) {
		entry.top = top;

		updateMessageSize(entry);
		updateMessageHeight(entry);

		top += entry.height;
	}
	updateGeometries();
}

void MessagesUi::updateGeometries() {
	const auto scrollBottom = (_scroll->scrollTop() + _scroll->height());
	const auto atBottom = (scrollBottom >= _messages->height());

	const auto height = _views.empty()
		? 0
		: (_views.back().top + _views.back().height);
	_messages->setGeometry(0, 0, _width, height);

	const auto min = std::min(height, _availableHeight);
	_scroll->setGeometry(_left, _bottom - min, _width, min);

	if (atBottom) {
		_scroll->scrollToY(std::max(height - _scroll->height(), 0));
	}
}

void MessagesUi::move(int left, int bottom, int width, int availableHeight) {
	if (_left != left
		|| _bottom != bottom
		|| _width != width
		|| _availableHeight != availableHeight) {
		_left = left;
		_bottom = bottom;
		_width = width;
		_availableHeight = availableHeight;
		applyWidth();
	}
}

void MessagesUi::raise() {
	if (_scroll) {
		_scroll->raise();
	}
	for (const auto &view : _views) {
		if (const auto widget = view.reactionWidget.get()) {
			widget->raise();
		}
	}
}

rpl::lifetime &MessagesUi::lifetime() {
	return _lifetime;
}

} // namespace Calls::Group

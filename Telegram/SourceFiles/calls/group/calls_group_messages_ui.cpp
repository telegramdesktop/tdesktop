/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_messages_ui.h"

#include "base/unixtime.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/premium_preview_box.h"
#include "calls/group/ui/calls_group_stars_coloring.h"
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
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_text_badge.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/widgets/popup_menu.h"
#include "ui/color_int_conversion.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "styles/style_calls.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_info_levels.h"
#include "styles/style_media_view.h"
#include "styles/style_menu_icons.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>

namespace Calls::Group {
namespace {

constexpr auto kMessageBgOpacity = 0.8;
constexpr auto kDarkOverOpacity = 0.25;
constexpr auto kColoredMessageBgOpacity = 0.65;
constexpr auto kAdminBadgeTextOpacity = 0.6;

[[nodiscard]] int CountMessageRadius() {
	const auto minHeight = st::groupCallMessagePadding.top()
		+ st::messageTextStyle.font->height
		+ st::groupCallMessagePadding.bottom();
	return minHeight / 2;
}

[[nodiscard]] int CountPriceRadius() {
	const auto height = st::groupCallPricePadding.top()
		+ st::normalFont->height
		+ st::groupCallPricePadding.bottom();
	return height / 2;
}

[[nodiscard]] int CountPinnedRadius() {
	const auto height = st::groupCallUserpicPadding.top()
		+ st::groupCallPinnedUserpic
		+ st::groupCallUserpicPadding.bottom();
	return height / 2;
}

[[nodiscard]] uint64 ColoringKey(const Ui::StarsColoring &value) {
	return uint64(uint32(value.bgLight))
		| (uint64(uint32(value.bgDark)) << 32);
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

[[nodiscard]] base::unique_qptr<Ui::Menu::ItemBase> MakeMessageDateAction(
		not_null<Ui::PopupMenu*> menu,
		TimeId value) {
	const auto parent = menu->menu();

	const auto parsed = base::unixtime::parse(value);
	const auto date = parsed.date();
	const auto time = QLocale().toString(
		parsed.time(),
		QLocale::ShortFormat);
	const auto today = QDateTime::currentDateTime().date();
	const auto text = (date == today)
		? tr::lng_context_sent_today(tr::now, lt_time, time)
		: (date.addDays(1) == today)
		? tr::lng_context_sent_yesterday(tr::now, lt_time, time)
		: tr::lng_context_sent_date(
			tr::now,
			lt_date,
			langDayOfMonthFull(date),
			lt_time,
			time);
	const auto action = Ui::Menu::CreateAction(parent, text, [] {});
	action->setDisabled(true);
	return base::make_unique_q<Ui::Menu::Action>(
		menu->menu(),
		st::storiesCommentSentAt,
		action,
		nullptr,
		nullptr);
}

void ShowDeleteMessageConfirmation(
		std::shared_ptr<Ui::Show> show,
		MsgId id,
		not_null<PeerData*> from,
		bool canModerate,
		Fn<void(MessageDeleteRequest)> callback) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::variable<bool> report;
			rpl::variable<bool> all;
			rpl::variable<bool> ban;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto confirmed = [=](Fn<void()> close) {
			callback(MessageDeleteRequest{
				.id = id,
				.deleteAllFrom = state->all.current() ? from.get() : nullptr,
				.ban = state->ban.current() ? from.get() : nullptr,
				.reportSpam = state->report.current(),
			});
			close();
		};
		Ui::ConfirmBox(box, {
			.text = tr::lng_selected_delete_sure_this(),
			.confirmed = confirmed,
			.confirmText = tr::lng_box_delete(),
			.labelStyle = &st::groupCallBoxLabel,
		});
		if (canModerate) {
			const auto check = [&](rpl::producer<QString> text) {
				const auto add = st::groupCallCheckbox.margin;
				const auto added = QMargins(0, add.top(), 0, add.bottom());
				const auto margin = st::boxRowPadding + added;

				return box->addRow(object_ptr<Ui::Checkbox>(
					box,
					std::move(text),
					false,
					st::groupCallCheckbox
				), margin)->checkedValue();
			};
			state->report = check(tr::lng_report_spam());
			state->all = check(tr::lng_delete_all_from_user(
				lt_user,
				rpl::single(from->shortName()))),
			state->ban = check(tr::lng_ban_user());
		}
	}));
}

[[nodiscard]] QImage CrownMask(int place) {
	const auto &icon = st::paidReactCrownSmall;
	const auto size = icon.size();
	const auto ratio = style::DevicePixelRatio();
	const auto full = size * ratio;
	auto result = QImage(full, QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(ratio);

	auto p = QPainter(&result);
	icon.paint(p, 0, 0, size.width(), QColor(255, 255, 255));

	const auto top = st::paidReactCrownSmallTop;
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.setPen(Qt::transparent);
	p.setFont(st::levelStyle.font);
	p.drawText(
		QRect(0, top, icon.width(), icon.height()),
		QString::number(place),
		style::al_top);
	p.end();

	return result;
}

} // namespace

struct MessagesUi::MessageView {
	MsgId id = 0;
	MsgId sendingId = 0;
	not_null<PeerData*> from;
	ClickHandlerPtr fromLink;
	Ui::Animations::Simple toggleAnimation;
	Ui::Animations::Simple sentAnimation;
	Data::ReactionId reactionId;
	std::unique_ptr<Ui::InfiniteRadialAnimation> sendingAnimation;
	std::unique_ptr<Ui::ReactionFlyAnimation> reactionAnimation;
	std::unique_ptr<Ui::RpWidget> reactionWidget;
	QPoint reactionShift;
	TextWithEntities original;
	Ui::PeerUserpicView view;
	Ui::Text::String name;
	Ui::Text::String text;
	Ui::Text::String price;
	TimeId date = 0;
	int stars = 0;
	int place = 0;
	int top = 0;
	int width = 0;
	int left = 0;
	int height = 0;
	int realHeight = 0;
	int nameWidth = 0;
	int textLeft = 0;
	int textTop = 0;
	bool removed = false;
	bool sending = false;
	bool failed = false;
	bool simple = false;
	bool admin = false;
	bool mine = false;
};

struct MessagesUi::PinnedView {
	MsgId id = 0;
	not_null<PeerData*> from;
	Ui::Animations::Simple toggleAnimation;
	Ui::PeerUserpicView view;
	Ui::Text::String text;
	crl::time duration = 0;
	crl::time end = 0;
	int stars = 0;
	int place = 0;
	int top = 0;
	int width = 0;
	int left = 0;
	int height = 0;
	int realWidth = 0;
	bool removed = false;
	bool requiresSmooth = false;
};

MessagesUi::PayedBg::PayedBg(const Ui::StarsColoring &coloring)
: light(Ui::ColorFromSerialized(coloring.bgLight))
, dark(Ui::ColorFromSerialized(coloring.bgDark))
, pinnedLight(CountPinnedRadius(), light.color())
, pinnedDark(CountPinnedRadius(), dark.color())
, messageLight(CountMessageRadius(), light.color())
, priceDark(CountPriceRadius(), dark.color())
, badgeDark(st::roundRadiusLarge, dark.color()) {
}

MessagesUi::MessagesUi(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	MessagesMode mode,
	rpl::producer<std::vector<Message>> messages,
	rpl::producer<std::vector<not_null<PeerData*>>> topDonorsValue,
	rpl::producer<MessageIdUpdate> idUpdates,
	rpl::producer<bool> canManageValue,
	rpl::producer<bool> shown)
: _parent(parent)
, _show(std::move(show))
, _mode(mode)
, _canManage(std::move(canManageValue))
, _messageBg([] {
	auto result = st::groupCallBg->c;
	result.setAlphaF(kMessageBgOpacity);
	return result;
})
, _messageBgRect(CountMessageRadius(), _messageBg.color())
, _crownHelper(Core::TextContext({ .session = &_show->session() }))
, _topDonors(std::move(topDonorsValue))
, _fadeHeight(st::normalFont->height * 2)
, _fadeWidth(st::normalFont->height * 2)
, _streamMode(_mode == MessagesMode::VideoStream) {
	setupBadges();
	setupList(std::move(messages), std::move(shown));
	handleIdUpdates(std::move(idUpdates));
}

MessagesUi::~MessagesUi() = default;

void MessagesUi::setupBadges() {
	auto helper = Ui::Text::CustomEmojiHelper();
	const auto liveText = helper.paletteDependent(
		Ui::Text::CustomEmojiTextBadge(
			tr::lng_video_stream_live(tr::now),
			st::groupCallMessageBadge,
			st::groupCallMessageBadgeMargin));
	_liveBadge.setMarkedText(
		st::messageTextStyle,
		liveText,
		kMarkupTextOptions,
		helper.context());

	_adminBadge.setText(st::messageTextStyle, tr::lng_admin_badge(tr::now));

	_topDonors.value(
	) | rpl::on_next([=] {
		for (auto &entry : _views) {
			const auto place = donorPlace(entry.from);
			if (entry.place != place) {
				entry.place = place;
				if (!entry.failed) {
					setContent(entry);
				}
			}
		}
		for (auto &entry : _pinnedViews) {
			const auto place = donorPlace(entry.from);
			if (entry.place != place) {
				entry.place = place;
				setContent(entry);
			}
		}
		applyGeometry();
	}, _lifetime);
}

void MessagesUi::setupList(
		rpl::producer<std::vector<Message>> messages,
		rpl::producer<bool> shown) {
	rpl::combine(
		std::move(messages),
		std::move(shown)
	) | rpl::on_next([=](std::vector<Message> &&list, bool shown) {
		if (shown) {
			_hidden = std::nullopt;
		} else {
			_hidden = base::take(list);
		}
		showList(list);
	}, _lifetime);
}

void MessagesUi::showList(const std::vector<Message> &list) {
	const auto now = base::unixtime::now();
	auto from = begin(list);
	auto till = end(list);
	for (auto &entry : _views) {
		if (entry.removed) {
			continue;
		}
		const auto id = entry.id;
		const auto i = ranges::find(
			from,
			till,
			id,
			&Message::id);
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
		entry.date = i->date;
		if (i == from) {
			appendPinned(*i, now);
			++from;
		}
	}
	auto addedSendingToBottom = false;
	for (auto i = from; i != till; ++i) {
		const auto j = ranges::find(_views, i->id, &MessageView::id);
		if (j != end(_views)) {
			if (!j->removed) {
				continue;
			}
			if (j->failed != i->failed) {
				setContentFailed(*j);
				updateMessageSize(*j);
				repaintMessage(j->id);
			} else if (j->sending != (i->date == 0)) {
				animateMessageSent(*j);
			}
			j->date = i->date;
			toggleMessage(*j, true);
		} else {
			if (i + 1 == till && !i->date) {
				addedSendingToBottom = true;
			}
			appendMessage(*i);
			appendPinned(*i, now);
		}
	}
	if (addedSendingToBottom) {
		const auto from = _scroll->scrollTop();
		const auto till = _scroll->scrollTopMax();
		if (from >= till) {
			return;
		}
		_scrollToAnimation.stop();
		_scrollToAnimation.start([=] {
			_scroll->scrollToY(_scroll->scrollTopMax()
				- _scrollToAnimation.value(0));
		}, till - from, 0, st::slideDuration, anim::easeOutCirc);
	}
}

void MessagesUi::handleIdUpdates(rpl::producer<MessageIdUpdate> idUpdates) {
	std::move(
		idUpdates
	) | rpl::on_next([=](MessageIdUpdate update) {
		const auto i = ranges::find(
			_views,
			update.localId,
			&MessageView::id);
		if (i == end(_views)) {
			return;
		}
		i->sendingId = update.localId;
		i->id = update.realId;
		if (_revealedSpoilerId == update.localId) {
			_revealedSpoilerId = update.realId;
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
	const auto &pricePadding = st::groupCallPricePadding;

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
	const auto price = entry.simple
		? (pricePadding.left() + pricePadding.right() + entry.price.maxWidth())
		: 0;
	const auto space = st::normalFont->spacew;
	const auto nameWidth = entry.name.isEmpty() ? 0 : entry.name.maxWidth();
	const auto nameLineWidth = nameWidth
		? (nameWidth
			+ space
			+ _liveBadge.maxWidth()
			+ space
			+ _adminBadge.maxWidth())
		: 0;

	const auto nameHeight = entry.name.isEmpty()
		? 0
		: st::messageTextStyle.font->height;
	const auto textHeight = size.height();
	entry.width = widthSkip
		+ std::max(size.width() + price, std::min(nameLineWidth, inner));
	entry.left = _streamMode ? 0 : (_width - entry.width) / 2;
	entry.textLeft = leftSkip;
	entry.textTop = padding.top() + nameHeight;
	entry.nameWidth = std::min(
		nameWidth,
		(entry.width
			- widthSkip
			- space
			- _liveBadge.maxWidth()
			- space
			- _adminBadge.maxWidth()));
	updateReactionPosition(entry);

	const auto contentHeight = entry.textTop + textHeight + padding.bottom();
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

void MessagesUi::updatePinnedSize(PinnedView &entry) {
	const auto &padding = st::groupCallPinnedPadding;

	const auto userpicPadding = st::groupCallUserpicPadding;
	const auto userpicSize = st::groupCallPinnedUserpic;
	const auto leftSkip = userpicPadding.left()
		+ userpicSize
		+ userpicPadding.right();
	const auto inner = std::min(
		entry.text.maxWidth(),
		st::groupCallPinnedMaxWidth);

	entry.height = userpicPadding.top()
		+ userpicSize
		+ userpicPadding.bottom();
	entry.top = 0;

	const auto skip = st::groupCallMessageSkip;
	entry.realWidth = skip + leftSkip + inner + padding.right();

	const auto ratio = style::DevicePixelRatio();
	entry.requiresSmooth = (entry.realWidth * ratio * 1000 > entry.duration);
}

bool MessagesUi::updatePinnedWidth(PinnedView &entry) {
	const auto width = entry.toggleAnimation.animating()
		? anim::interpolate(
			0,
			entry.realWidth,
			entry.toggleAnimation.value(entry.removed ? 0. : 1.))
		: entry.realWidth;
	if (entry.width == width) {
		return false;
	}
	entry.width = width;
	return true;
}

void MessagesUi::setContentFailed(MessageView &entry) {
	entry.failed = true;
	entry.name = Ui::Text::String();
	entry.text = Ui::Text::String(
		st::messageTextStyle,
		TextWithEntities().append(
			QString::fromUtf8("\xe2\x9d\x97\xef\xb8\x8f")
		).append(' ').append(
			tr::italic(u"Failed to send the message."_q)),
		kMarkupTextOptions,
		st::groupCallWidth / 8);
	entry.price = Ui::Text::String();
}

void MessagesUi::setContent(MessageView &entry) {
	entry.simple = !entry.admin && entry.original.empty() && entry.stars > 0;

	const auto name = nameText(entry.from, entry.place);
	entry.name = entry.admin
		? Ui::Text::String(
			st::messageTextStyle,
			name,
			kMarkupTextOptions,
			Ui::kQFixedMax,
			_crownHelper.context())
		: Ui::Text::String();
	if (const auto stars = entry.stars) {
		entry.price = Ui::Text::String(
			entry.simple ? st::messageTextStyle : st::whoReadDateStyle,
			Ui::Text::IconEmoji(
				&st::starIconEmojiSmall
			).append(Lang::FormatCountDecimal(stars)),
			kMarkupTextOptions);
	} else {
		entry.price = Ui::Text::String();
	}
	auto composed = entry.admin
		? entry.original
		: tr::link(name, 1).append(' ').append(entry.original);
	if (!entry.admin) {
		composed.text.replace(QChar('\n'), QChar(' '));
	}
	entry.text = Ui::Text::String(
		st::messageTextStyle,
		composed,
		kMarkupTextOptions,
		st::groupCallWidth / 8,
		_crownHelper.context([this, id = entry.id] { repaintMessage(id); }));
	if (!entry.simple && !entry.price.isEmpty()) {
		entry.text.updateSkipBlock(
			entry.price.maxWidth(),
			st::normalFont->height);
	}
	if (!entry.simple && !entry.admin) {
		entry.text.setLink(1, entry.fromLink);
	}
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

void MessagesUi::setContent(PinnedView &entry) {
	const auto text = nameText(entry.from, entry.place);
	entry.text.setMarkedText(
		st::messageTextStyle,
		text,
		kMarkupTextOptions,
		_crownHelper.context());
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

void MessagesUi::repaintMessage(MsgId id) {
	auto i = ranges::find(_views, id, &MessageView::id);
	if (i == end(_views) && id < 0) {
		i = ranges::find(_views, id, &MessageView::sendingId);
	}
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
	if (!i->toggleAnimation.animating() && id == _delayedHighlightId) {
		highlightMessage(base::take(_delayedHighlightId));
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
		_scrollToAnimation.stop();
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

	const auto id = data.id;
	const auto peer = data.peer;
	auto &entry = _views.emplace_back(MessageView{
		.id = id,
		.from = peer,
		.original = data.text,
		.date = data.date,
		.stars = data.stars,
		.place = donorPlace(peer),
		.top = top,
		.sending = !data.date,
		.admin = data.admin && _streamMode,
		.mine = data.mine,
	});
	const auto repaint = [=] {
		repaintMessage(id);
	};
	entry.fromLink = std::make_shared<LambdaClickHandler>([=] {
		_show->show(
			PrepareShortInfoBox(peer, _show, &st::storiesShortInfoBox));
	});
	if (data.failed) {
		setContentFailed(entry);
	} else {
		setContent(entry);
	}
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

void MessagesUi::togglePinned(PinnedView &entry, bool shown) {
	const auto id = entry.id;
	entry.removed = !shown;
	entry.toggleAnimation.start(
		[=] { repaintPinned(id); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration,
		shown ? anim::easeOutCirc : anim::easeInCirc);
	repaintPinned(id);
}

void MessagesUi::repaintPinned(MsgId id) {
	const auto i = ranges::find(_pinnedViews, id, &PinnedView::id);
	if (i == end(_pinnedViews)) {
		return;
	} else if (i->removed && !i->toggleAnimation.animating()) {
		const auto left = i->left;
		recountWidths(_pinnedViews.erase(i), left);
		return;
	}
	if (i->toggleAnimation.animating() || i->width != i->realWidth) {
		const auto was = i->width;
		if (updatePinnedWidth(*i)) {
			if (i->width > was) {
				const auto larger = countPinnedScrollSkip(*i);
				if (larger > _pinnedScrollSkip) {
					setPinnedScrollSkip(larger);
				}
			} else {
				applyGeometryToPinned();
			}
			recountWidths(i, i->left);
			return;
		}
	}
	_pinned->update(i->left, 0, i->width, _pinned->height());
}

void MessagesUi::recountWidths(
		std::vector<PinnedView>::iterator i,
		int left) {
	auto from = left;
	for (auto e = end(_pinnedViews); i != e; ++i) {
		i->left = left;
		left += i->width;
	}
	if (_pinnedViews.empty()) {
		delete base::take(_pinned);
		_pinnedScroll = nullptr;
	} else {
		updateGeometries();
		_pinned->update(from, 0, left - from, _pinned->height());
	}
}

void MessagesUi::appendPinned(const Message &data, TimeId now) {
	if (!data.date
		|| data.pinFinishDate <= data.date
		|| data.pinFinishDate <= now
		|| ranges::contains(
			_pinnedViews,
			data.id,
			&PinnedView::id)) {
		return;
	}

	const auto peer = data.peer;
	const auto finishes = crl::now()
		+ (data.pinFinishDate - now) * crl::time(1000);
	const auto i = ranges::find(_pinnedViews, peer, &PinnedView::from);
	if (i != end(_pinnedViews)) {
		if (i->end > finishes) {
			return;
		}
		const auto left = i->left;
		recountWidths(_pinnedViews.erase(i), left);
	}

	if (!_pinnedScroll) {
		setupPinnedWidget();
	}
	const auto j = ranges::lower_bound(
		_pinnedViews,
		data.stars,
		ranges::greater(),
		&PinnedView::stars);
	const auto left = (j != end(_pinnedViews))
		? j->left
		: _pinnedViews.empty()
		? 0
		: (_pinnedViews.back().left + _pinnedViews.back().width);
	auto &entry = *_pinnedViews.insert(j, PinnedView{
		.id = data.id,
		.from = peer,
		.duration = (data.pinFinishDate - data.date) * crl::time(1000),
		.end = finishes,
		.stars = data.stars,
		.place = donorPlace(peer),
		.left = left,
	});
	setContent(entry);
	updatePinnedSize(entry);
	entry.width = 0;
	togglePinned(entry, true);
}

int MessagesUi::donorPlace(not_null<PeerData*> peer) const {
	const auto &donors = _topDonors.current();
	const auto i = ranges::find(donors, peer);
	if (i == end(donors)) {
		return 0;
	}
	return static_cast<int>(std::distance(begin(donors), i)) + 1;
}

TextWithEntities MessagesUi::nameText(
		not_null<PeerData*> peer,
		int place) {
	auto result = TextWithEntities();
	if (place > 0) {
		auto i = _crownEmojiDataCache.find(place);
		if (i == _crownEmojiDataCache.end()) {
			i = _crownEmojiDataCache.emplace(
				place,
				_crownHelper.imageData(Ui::Text::ImageEmoji{
					.image = CrownMask(place),
					.margin = st::paidReactCrownMargin,
				})).first;
		}
		result.append(Ui::Text::SingleCustomEmoji(i->second)).append(' ');
	}
	result.append(tr::bold(peer->shortName()));
	return result;
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
		) | rpl::on_next([=](int yshift, QPoint point) {
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
	raw->paintRequest() | rpl::on_next([=] {
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

void MessagesUi::updateLeftFade() {
	const auto leftFadeShown = (_pinnedScroll->scrollLeft() > 0);
	if (_leftFadeShown != leftFadeShown) {
		_leftFadeShown = leftFadeShown;
		//const auto from = leftFadeShown ? 0. : 1.;
		//const auto till = rightFadeShown ? 1. : 0.;
		//_leftFadeAnimation.start([=] {
			_pinned->update(
				_pinnedScroll->scrollLeft(),
				0,
				_fadeWidth,
				_pinned->height());
		//}, from, till, st::slideWrapDuration);
	}
}

void MessagesUi::updateRightFade() {
	const auto max = _pinnedScroll->scrollLeftMax();
	const auto rightFadeShown = (_pinnedScroll->scrollLeft() < max);
	if (_rightFadeShown != rightFadeShown) {
		_rightFadeShown = rightFadeShown;
		//const auto from = rightFadeShown ? 0. : 1.;
		//const auto till = rightFadeShown ? 1. : 0.;
		//_rightFadeAnimation.start([=] {
			_pinned->update(
				(_pinnedScroll->scrollLeft()
					+ _pinnedScroll->width()
					- _fadeWidth),
				0,
				_fadeWidth,
				_pinned->height());
		//}, from, till, st::slideWrapDuration);
	}
}

void MessagesUi::setupMessagesWidget() {
	_scroll = std::make_unique<Ui::ElasticScroll>(
		_parent,
		st::groupCallMessagesScroll);
	const auto scroll = _scroll.get();

	_messages = scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(scroll));
	_messages->move(0, 0);
	rpl::combine(
		scroll->scrollTopValue(),
		scroll->heightValue(),
		_messages->heightValue()
	) | rpl::on_next([=] {
		updateTopFade();
		updateBottomFade();
	}, scroll->lifetime());

	if (_mode == MessagesMode::GroupCall) {
		receiveSomeMouseEvents();
	} else {
		receiveAllMouseEvents();
	}

	_messages->paintRequest() | rpl::on_next([=](QRect clip) {
		const auto start = scroll->scrollTop();
		const auto end = start + scroll->height();
		const auto ratio = style::DevicePixelRatio();
		const auto session = &_show->session();
		const auto &colorings = session->appConfig().groupCallColorings();

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
			const auto x = entry.left;
			const auto y = entry.top;
			const auto use = entry.realHeight - skip;
			const auto width = entry.width;
			p.setPen(Qt::NoPen);
			const auto scaled = (entry.height < entry.realHeight);
			if (scaled) {
				const auto used = entry.height - skip;
				const auto mx = scaled ? (x + (width / 2)) : 0;
				const auto my = scaled ? (y + (used / 2)) : 0;
				const auto scale = used / float64(use);
				p.save();
				p.translate(mx, my);
				p.scale(scale, scale);
				p.setOpacity(scale);
				p.translate(-mx, -my);
			}
			auto bg = (std::unique_ptr<PayedBg>*)nullptr;
			if (!_streamMode) {
				_messageBgRect.paint(p, { x, y, width, use });
			} else if (entry.stars) {
				const auto coloring = Ui::StarsColoringForCount(
					colorings,
					entry.stars);
				bg = &_bgs[ColoringKey(coloring)];
				if (!*bg) {
					*bg = std::make_unique<PayedBg>(coloring);
				}
				p.setOpacity(kColoredMessageBgOpacity);
				(*bg)->messageLight.paint(p, { x, y, width, use });
				p.setOpacity(1.);
				if (_highlightAnimation.animating()
					&& entry.id == _highlightId) {
					const auto radius = CountMessageRadius();
					const auto progress = _highlightAnimation.value(3.);
					p.setBrush(st::white);
					p.setOpacity(
						std::min((1.5 - std::abs(1.5 - progress)), 1.));
					auto hq = PainterHighQualityEnabler(p);
					p.drawRoundedRect(x, y, width, use, radius, radius);
					p.setOpacity(1.);
				}
			} else if (entry.admin) {
				_messageBgRect.paint(p, { x, y, width, use });
			}

			const auto textLeft = entry.textLeft;
			const auto hasUserpic = !entry.failed;
			if (hasUserpic) {
				const auto userpicSize = st::groupCallUserpic;
				const auto userpicPadding = st::groupCallUserpicPadding;
				const auto position = QPoint(
					x + userpicPadding.left(),
					y + userpicPadding.top());
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
			}

			p.setPen(st::white);
			if (!entry.name.isEmpty()) {
				const auto space = st::normalFont->spacew;
				entry.name.draw(p, {
					.position = {
						x + textLeft,
						y + padding.top(),
					},
					.availableWidth = entry.nameWidth,
					.palette = &st::groupCallMessagePalette,
					.elisionLines = 1,
				});
				const auto liveLeft = x + textLeft + entry.nameWidth + space;
				_liveBadge.draw(p, {
					.position = { liveLeft, y + padding.top() },
				});

				p.setOpacity(kAdminBadgeTextOpacity);
				const auto adminLeft = x
					+ entry.width
					- padding.right()
					- _adminBadge.maxWidth();
				_adminBadge.draw(p, {
					.position = { adminLeft, y + padding.top() },
				});
				p.setOpacity(1.);
			}
			const auto pricePadding = st::groupCallPricePadding;
			const auto textRight = padding.right()
				+ (entry.simple
					? (entry.price.maxWidth()
						+ pricePadding.left()
						+ pricePadding.right())
					: 0);
			entry.text.draw(p, {
				.position = {
					x + textLeft,
					y + entry.textTop,
				},
				.availableWidth = entry.width - textLeft - textRight,
				.palette = &st::groupCallMessagePalette,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.now = now,
				.paused = !_messages->window()->isActiveWindow(),
			});
			if (!entry.price.isEmpty()) {
				const auto priceRight = x
					+ entry.width
					- entry.price.maxWidth();
				const auto priceLeft = entry.simple
					? (priceRight
						- (padding.top() - pricePadding.top())
						- pricePadding.right())
					: (priceRight - (padding.right() / 2));
				const auto priceTop = entry.simple
					? (y + entry.textTop)
					: (y + use - st::normalFont->height);
				if (entry.simple && bg) {
					p.setOpacity(kDarkOverOpacity);
					const auto r = QRect(
						priceLeft,
						priceTop,
						entry.price.maxWidth(),
						st::normalFont->height
					).marginsAdded(pricePadding);
					(*bg)->priceDark.paint(p, r);
					p.setOpacity(1.);
				}
				entry.price.draw(p, {
					.position = { priceLeft, priceTop },
					.availableWidth = entry.price.maxWidth(),
				});
			}
			if (!scaled && entry.reactionId && !entry.reactionAnimation) {
				startReactionAnimation(entry);
			}

			if (scaled) {
				p.restore();
			}
		}
		p.translate(0, start);

		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.setPen(Qt::NoPen);

		const auto topFade = (//_topFadeAnimation.value(
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
		const auto bottomFade = (//_bottomFadeAnimation.value(
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
	}, _messages->lifetime());

	scroll->show();
	applyGeometry();
}

void MessagesUi::receiveSomeMouseEvents() {
	ReceiveSomeMouseEvents(_scroll.get(), [=](QPoint point) {
		for (const auto &entry : _views) {
			if (entry.failed || entry.top + entry.height <= point.y()) {
				continue;
			} else if (entry.top < point.y()
				&& entry.left < point.x()
				&& entry.left + entry.width > point.x()) {
				handleClick(entry, point);
				return true;
			}
			break;
		}
		return false;
	});
}

void MessagesUi::receiveAllMouseEvents() {
	_messages->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type != QEvent::MouseButtonPress) {
			return;
		}
		const auto m = static_cast<QMouseEvent*>(e.get());
		const auto point = m->pos();
		for (const auto &entry : _views) {
			if (entry.failed || entry.top + entry.height <= point.y()) {
				continue;
			} else if (entry.top < point.y()
				&& entry.left < point.x()
				&& entry.left + entry.width > point.x()) {
				if (m->button() == Qt::LeftButton) {
					handleClick(entry, point);
				} else {
					showContextMenu(entry, m->globalPos());
				}
			}
			break;
		}
	}, _messages->lifetime());
}

void MessagesUi::handleClick(const MessageView &entry, QPoint point) {
	const auto padding = st::groupCallMessagePadding;
	const auto userpicSize = st::groupCallUserpic;
	const auto userpicPadding = st::groupCallUserpicPadding;
	const auto userpic = QRect(
		entry.left + userpicPadding.left(),
		entry.top + userpicPadding.top(),
		userpicSize,
		userpicSize);
	const auto name = entry.name.isEmpty()
		? QRect()
		: QRect(
			entry.left + entry.textLeft,
			entry.top + padding.top(),
			entry.nameWidth,
			st::messageTextStyle.font->height);
	const auto link = (userpic.contains(point) || name.contains(point))
		? entry.fromLink
		: entry.text.getState(point - QPoint(
			entry.left + entry.textLeft,
			entry.top + entry.textTop
		), entry.width - entry.textLeft - padding.right()).link;
	if (link) {
		ActivateClickHandler(_messages, link, Qt::LeftButton);
	}
}

void MessagesUi::showContextMenu(
		const MessageView &entry,
		QPoint globalPoint) {
	if (_menu || !entry.date || entry.failed) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_parent,
		st::groupCallPopupMenuWithIcons);
	_menu->addAction(MakeMessageDateAction(_menu.get(), entry.date));
	const auto &original = entry.original;
	const auto canCopy = !original.empty();
	const auto canDelete = entry.mine || _canManage.current();
	if (canCopy || canDelete) {
		_menu->addSeparator(&st::mediaviewWideMenuSeparator);
	}
	if (canCopy) {
		_menu->addAction(tr::lng_context_copy_text(tr::now), [=] {
			TextUtilities::SetClipboardText(
				TextForMimeData::WithExpandedLinks(original));
		}, &st::mediaMenuIconCopy);
	}
	if (canDelete) {
		const auto id = entry.id;
		const auto from = entry.from;
		const auto canModerate = _canManage.current() && !entry.mine;
		_menu->addAction(tr::lng_context_delete_msg(tr::now), [=] {
			ShowDeleteMessageConfirmation(_show, id, from, canModerate, [=](
					MessageDeleteRequest request) {
				_deleteRequests.fire_copy(request);
			});
		}, &st::mediaMenuIconDelete);
	}
	_menu->popup(globalPoint);
}

void MessagesUi::setupPinnedWidget() {
	_pinnedScroll = std::make_unique<Ui::ElasticScroll>(
		_parent,
		st::groupCallMessagesScroll,
		Qt::Horizontal);
	const auto scroll = _pinnedScroll.get();

	_pinned = scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(scroll));
	_pinned->move(0, 0);
	rpl::combine(
		scroll->scrollLeftValue(),
		scroll->widthValue(),
		_pinned->widthValue()
	) | rpl::on_next([=] {
		updateLeftFade();
		updateRightFade();
	}, scroll->lifetime());

	struct Animation {
		base::Timer seconds;
		Ui::Animations::Simple smooth;
		bool requiresSmooth = false;
	};
	const auto animation = _pinned->lifetime().make_state<Animation>();
	animation->seconds.setCallback([=] {
		const auto now = crl::now();
		auto smooth = false;
		auto off = base::flat_set<MsgId>();
		for (auto &entry : _pinnedViews) {
			if (entry.removed) {
				continue;
			} else if (entry.end <= now) {
				off.emplace(entry.id);
				entry.requiresSmooth = false;
			} else if (entry.requiresSmooth) {
				smooth = true;
			}
		}
		if (smooth && !anim::Disabled()) {
			animation->smooth.start([=] {
				_pinned->update();
			}, 0., 1., 900.);
		} else {
			_pinned->update();
		}
		for (const auto &id : off) {
			const auto i = ranges::find(_pinnedViews, id, &PinnedView::id);
			if (i != end(_pinnedViews)) {
				togglePinned(*i, false);
			}
		}
	});
	animation->seconds.callEach(crl::time(1000));

	_pinned->paintRequest() | rpl::on_next([=](QRect clip) {
		const auto session = &_show->session();
		const auto &colorings = session->appConfig().groupCallColorings();
		const auto start = scroll->scrollLeft();
		const auto end = start + scroll->width();
		const auto ratio = style::DevicePixelRatio();

		if ((_pinnedCanvas.width() < scroll->width() * ratio)
			|| (_pinnedCanvas.height() < scroll->height() * ratio)) {
			_pinnedCanvas = QImage(
				scroll->size() * ratio,
				QImage::Format_ARGB32_Premultiplied);
			_pinnedCanvas.setDevicePixelRatio(ratio);
		}
		auto p = Painter(&_pinnedCanvas);

		p.setCompositionMode(QPainter::CompositionMode_Clear);
		p.fillRect(QRect(QPoint(), scroll->size()), QColor(0, 0, 0, 0));

		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		const auto now = crl::now();
		const auto skip = st::groupCallMessageSkip;
		const auto padding = st::groupCallPinnedPadding;
		p.translate(-start, 0);
		for (auto &entry : _pinnedViews) {
			if (entry.width <= skip || entry.left + entry.width <= start) {
				continue;
			} else if (entry.left >= end) {
				break;
			}
			const auto x = entry.left;
			const auto y = entry.top;
			const auto use = entry.realWidth - skip;
			const auto height = entry.height;
			p.setPen(Qt::NoPen);
			const auto scaled = (entry.width < entry.realWidth);
			if (scaled) {
				const auto used = entry.width - skip;
				const auto mx = scaled ? (x + (used / 2)) : 0;
				const auto my = scaled ? (y + (height / 2)) : 0;
				const auto scale = used / float64(use);
				p.save();
				p.translate(mx, my);
				p.scale(scale, scale);
				p.setOpacity(scale);
				p.translate(-mx, -my);
			}
			const auto coloring = Ui::StarsColoringForCount(
				colorings,
				entry.stars);
			auto &bg = _bgs[ColoringKey(coloring)];
			if (!bg) {
				bg = std::make_unique<PayedBg>(coloring);
			}
			const auto still = (entry.end - now) / float64(entry.duration);
			const auto part = int(base::SafeRound(still * use));
			const auto line = st::lineWidth;
			if (part > 0) {
				p.setOpacity(kColoredMessageBgOpacity);
				bg->pinnedLight.paint(p, { x, y, use, height });
			}
			if (part < use) {
				p.setClipRect(x + part, y, use - part + line, height);
				p.setOpacity(kDarkOverOpacity);
				bg->pinnedDark.paint(p, { x, y, use, height });
			}
			p.setClipping(false);
			p.setOpacity(1.);

			const auto userpicSize = st::groupCallPinnedUserpic;
			const auto userpicPadding = st::groupCallUserpicPadding;
			const auto position = QPoint(
				x + userpicPadding.left(),
				y + userpicPadding.top());
			entry.from->paintUserpic(p, entry.view, {
				.position = position,
				.size = userpicSize,
				.shape = Ui::PeerUserpicShape::Circle,
			});
			const auto leftSkip = userpicPadding.left()
				+ userpicSize
				+ userpicPadding.right();

			p.setPen(st::white);
			entry.text.draw(p, {
				.position = { x + leftSkip, y + padding.top() },
				.availableWidth = entry.width - leftSkip - padding.right(),
				.elisionLines = 1,
			});
			if (scaled) {
				p.restore();
			}
		}
		p.translate(start, 0);

		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.setPen(Qt::NoPen);

		const auto leftFade = (//_leftFadeAnimation.value(
			_leftFadeShown ? 1. : 0.);
		if (leftFade) {
			auto gradientLeft = QLinearGradient(0, 0, _fadeWidth, 0);
			gradientLeft.setStops({
				{ 0., QColor(255, 255, 255, 0) },
				{ 1., QColor(255, 255, 255, 255) },
			});
			p.setOpacity(leftFade);
			p.setBrush(gradientLeft);
			p.drawRect(0, 0, _fadeWidth, scroll->height());
			p.setOpacity(1.);
		}
		const auto rightFade = (//_rightFadeAnimation.value(
			_rightFadeShown ? 1. : 0.);
		if (rightFade) {
			const auto till = scroll->width();
			const auto from = till - _fadeWidth;
			auto gradientRight = QLinearGradient(from, 0, till, 0);
			gradientRight.setStops({
				{ 0., QColor(255, 255, 255, 255) },
				{ 1., QColor(255, 255, 255, 0) },
			});
			p.setBrush(gradientRight);
			p.drawRect(from, 0, _fadeWidth, scroll->height());
		}
		QPainter(_pinned).drawImage(
			QRect(QPoint(start, 0), scroll->size()),
			_pinnedCanvas,
			QRect(QPoint(), scroll->size() * ratio));
	}, _pinned->lifetime());

	_pinned->setMouseTracking(true);
	const auto find = [=](QPoint position) {
		if (position.y() < 0 || position.y() >= _pinned->height()) {
			return MsgId();
		} else for (const auto &entry : _pinnedViews) {
			if (entry.left > position.x()) {
				break;
			} else if (entry.left + entry.width > position.x()) {
				return entry.id;
			}
		}
		return MsgId();
	};
	_pinned->events() | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			const auto pos = static_cast<QMouseEvent*>(e.get())->pos();
			if (const auto id = find(pos)) {
				if (_hidden) {
					showList(*base::take(_hidden));
					_hiddenShowRequested.fire({});
				}
				highlightMessage(id);
			}
		} else if (type == QEvent::MouseMove) {
			const auto pos = static_cast<QMouseEvent*>(e.get())->pos();
			_pinned->setCursor(find(pos)
				? style::cur_pointer
				: style::cur_default);
		}
	}, _pinned->lifetime());

	scroll->show();
	applyGeometry();
}

void MessagesUi::highlightMessage(MsgId id) {
	if (!_scroll) {
		return;
	}
	const auto i = ranges::find(_views, id, &MessageView::id);
	if (i == end(_views) || i->top < 0) {
		return;
	} else if (i->toggleAnimation.animating()) {
		_delayedHighlightId = id;
		return;
	}
	_delayedHighlightId = 0;
	const auto top = std::clamp(
		i->top - ((_scroll->height() - i->realHeight) / 2),
		0,
		i->top);
	const auto to = top - i->top;
	const auto from = _scroll->scrollTop() - i->top;
	if (from == to) {
		startHighlight(id);
		return;
	}
	_scrollToAnimation.stop();
	_scrollToAnimation.start([=] {
		const auto i = ranges::find(_views, id, &MessageView::id);
		if (i == end(_views)) {
			_scrollToAnimation.stop();
			return;
		}
		_scroll->scrollToY(i->top + _scrollToAnimation.value(to));
		if (!_scrollToAnimation.animating()) {
			startHighlight(id);
		}
	}, from, to, st::slideDuration, anim::easeOutCirc);
}

void MessagesUi::startHighlight(MsgId id) {
	_highlightId = id;
	_highlightAnimation.start([=] {
		repaintMessage(id);
	}, 0., 3., 1000);
}

void MessagesUi::applyGeometry() {
	if (_scroll) {
		auto top = 0;
		for (auto &entry : _views) {
			entry.top = top;

			updateMessageSize(entry);
			updateMessageHeight(entry);

			top += entry.height;
		}
	}
	applyGeometryToPinned();
	updateGeometries();
}

void MessagesUi::applyGeometryToPinned() {
	if (!_pinnedScroll) {
		setPinnedScrollSkip(0);
		return;
	}
	const auto skip = st::groupCallMessageSkip;
	auto maxHeight = 0;
	auto left = 0;
	for (auto &entry : _pinnedViews) {
		entry.left = left;
		updatePinnedSize(entry);
		updatePinnedWidth(entry);
		left += entry.width;

		if (maxHeight < entry.height + skip) {
			const auto possible = countPinnedScrollSkip(entry);
			maxHeight = std::max(possible, maxHeight);
		}
	}
	setPinnedScrollSkip(maxHeight);
}

int MessagesUi::countPinnedScrollSkip(const PinnedView &entry) const {
	const auto skip = st::groupCallMessageSkip;
	if (!entry.toggleAnimation.animating()) {
		return entry.height + skip;
	}
	const auto used = ((entry.height + skip) * entry.width)
		/ float64(entry.realWidth);
	return int(base::SafeRound(used));
}

void MessagesUi::setPinnedScrollSkip(int skip) {
	if (_pinnedScrollSkip != skip) {
		_pinnedScrollSkip = skip;
		updateGeometries();
	}
}

void MessagesUi::updateGeometries() {
	if (_pinnedScroll) {
		const auto skip = st::groupCallMessageSkip;
		const auto width = _pinnedViews.empty()
			? 0
			: (_pinnedViews.back().left + _pinnedViews.back().width - skip);
		const auto height = _pinnedViews.empty()
			? 0
			: _pinnedViews.back().height;
		const auto bottom = _bottom - st::groupCallMessageSkip;
		_pinned->resize(width, height);

		const auto min = std::min(width, _width);
		_pinnedScroll->setGeometry(_left, bottom - height, min, height);
	}
	if (_scroll) {
		const auto scrollBottom = (_scroll->scrollTop() + _scroll->height());
		const auto atBottom = (scrollBottom >= _messages->height());
		const auto bottom = _bottom - _pinnedScrollSkip;

		const auto height = _views.empty()
			? 0
			: (_views.back().top + _views.back().height);
		_messages->resize(_width, height);

		const auto min = std::min(height, _availableHeight);
		_scroll->setGeometry(_left, bottom - min, _width, min);

		if (atBottom) {
			_scroll->scrollToY(std::max(height - _scroll->height(), 0));
		}
	}
}

void MessagesUi::move(int left, int bottom, int width, int availableHeight) {
	const auto min = st::groupCallWidth * 2 / 6;
	if (width < min) {
		const auto add = min - width;
		width += add;
		left -= add / 2;
	}
	if (_left != left
		|| _bottom != bottom
		|| _width != width
		|| _availableHeight != availableHeight) {
		_left = left;
		_bottom = bottom;
		_width = width;
		_availableHeight = availableHeight;
		applyGeometry();
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

rpl::producer<> MessagesUi::hiddenShowRequested() const {
	return _hiddenShowRequested.events();
}

rpl::producer<MessageDeleteRequest> MessagesUi::deleteRequests() const {
	return _deleteRequests.events();
}

rpl::lifetime &MessagesUi::lifetime() {
	return _lifetime;
}

} // namespace Calls::Group

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_send.h"

#include "api/api_common.h"
#include "base/event_filter.h"
#include "base/unixtime.h"
#include "boxes/abstract_box.h"
#include "boxes/premium_preview_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "core/shortcuts.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_fake_items.h"
#include "history/view/history_view_schedule_box.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_unread_things.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_single_player.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_peer.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "settings/settings_premium.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

#include <QtWidgets/QApplication>

namespace SendMenu {
namespace {

constexpr auto kToggleDuration = crl::time(400);

class Delegate final : public HistoryView::DefaultElementDelegate {
public:
	Delegate(not_null<Ui::PathShiftGradient*> pathGradient)
	: _pathGradient(pathGradient) {
	}

private:
	bool elementAnimationsPaused() override {
		return false;
	}
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override {
		return _pathGradient;
	}
	HistoryView::Context elementContext() override {
		return HistoryView::Context::ContactPreview;
	}

	const not_null<Ui::PathShiftGradient*> _pathGradient;
};

class EffectPreview final : public Ui::RpWidget {
public:
	EffectPreview(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		Details details,
		QPoint position,
		const Data::Reaction &effect,
		Fn<void(Action, Details)> action,
		Fn<void()> done);

	void hideAnimated();

private:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

	[[nodiscard]] bool canSend() const;

	void setupGeometry(QPoint position);
	void setupBackground();
	void setupItem();
	void repaintBackground();
	void setupLottie();
	void setupSend(Details details);
	void createLottie();

	[[nodiscard]] bool ready() const;
	void paintLoading(QPainter &p);
	void paintLottie(QPainter &p);
	bool checkIconBecameLoaded();
	[[nodiscard]] bool checkLoaded();
	void toggle(bool shown);

	const EffectId _effectId = 0;
	const Data::Reaction _effect;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const std::shared_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _chatStyle;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	const std::unique_ptr<Delegate> _delegate;
	const not_null<History*> _history;
	const AdminLog::OwnedItem _replyTo;
	const AdminLog::OwnedItem _item;
	const std::unique_ptr<Ui::FlatButton> _send;
	const std::unique_ptr<Ui::PaddingWrap<Ui::FlatLabel>> _premiumPromoLabel;
	const not_null<Ui::RpWidget*> _bottom;
	const Fn<void()> _close;
	const Fn<void(Action, Details)> _actionWithEffect;

	QImage _icon;
	std::shared_ptr<Data::DocumentMedia> _media;
	QByteArray _bytes;
	QString _filepath;
	std::unique_ptr<Lottie::SinglePlayer> _lottie;

	QRect _inner;
	QImage _bg;
	QPoint _itemShift;
	QRect _iconRect;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _loading;

	Ui::Animations::Simple _shownAnimation;
	QPixmap _bottomCache;
	bool _hiding = false;

	rpl::lifetime _readyCheckLifetime;

};

class BottomRounded final : public Ui::FlatButton {
public:
	using FlatButton::FlatButton;

private:
	QImage prepareRippleMask() const override;
	void paintEvent(QPaintEvent *e) override;

};

QImage BottomRounded::prepareRippleMask() const {
	const auto fill = false;
	return Ui::RippleAnimation::MaskByDrawer(size(), fill, [&](QPainter &p) {
		const auto radius = st::previewMenu.radius;
		const auto expanded = rect().marginsAdded({ 0, 2 * radius, 0, 0 });
		p.drawRoundedRect(expanded, radius, radius);
	});
}

void BottomRounded::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = st::previewMenu.radius;
	const auto expanded = rect().marginsAdded({ 0, 2 * radius, 0, 0 });
	p.setPen(Qt::NoPen);
	const auto &st = st::previewMarkRead;
	if (isOver()) {
		p.setBrush(st.overBgColor);
	}
	p.drawRoundedRect(expanded, radius, radius);
	p.end();

	Ui::FlatButton::paintEvent(e);
}

[[nodiscard]] Data::PossibleItemReactionsRef LookupPossibleEffects(
		not_null<Main::Session*> session) {
	auto result = Data::PossibleItemReactionsRef();
	const auto reactions = &session->data().reactions();
	const auto &effects = reactions->list(Data::Reactions::Type::Effects);
	const auto premiumPossible = session->premiumPossible();
	auto added = base::flat_set<Data::ReactionId>();
	result.recent.reserve(effects.size());
	result.stickers.reserve(effects.size());
	for (const auto &reaction : effects) {
		if (premiumPossible || !reaction.premium) {
			if (added.emplace(reaction.id).second) {
				if (reaction.aroundAnimation) {
					result.recent.push_back(&reaction);
				} else {
					result.stickers.push_back(&reaction);
				}
			}
		}
	}
	return result;
}

[[nodiscard]] Fn<void(Action, Details)> ComposeActionWithEffect(
		Fn<void(Action, Details)> sendAction,
		EffectId id,
		Fn<void()> done) {
	return [=](Action action, Details details) {
		action.options.effectId = id;

		const auto onstack = done;
		sendAction(action, details);
		if (onstack) {
			onstack();
		}
	};
}

EffectPreview::EffectPreview(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	Details details,
	QPoint position,
	const Data::Reaction &effect,
	Fn<void(Action, Details)> action,
	Fn<void()> done)
: RpWidget(parent)
, _effectId(effect.id.custom())
, _effect(effect)
, _show(show)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _chatStyle(
	std::make_unique<Ui::ChatStyle>(
		_show->session().colorIndicesValue()))
, _pathGradient(
	HistoryView::MakePathShiftGradient(_chatStyle.get(), [=] { update(); }))
, _delegate(std::make_unique<Delegate>(_pathGradient.get()))
, _history(show->session().data().history(
	PeerData::kServiceNotificationsId))
, _replyTo(HistoryView::GenerateItem(
	_delegate.get(),
	_history,
	HistoryView::GenerateUser(
		_history,
		tr::lng_settings_chat_message_reply_from(tr::now)),
	FullMsgId(),
	tr::lng_settings_chat_message(tr::now)))
, _item(HistoryView::GenerateItem(
	_delegate.get(),
	_history,
	_history->peer->id,
	_replyTo->data()->fullId(),
	tr::lng_settings_chat_message_reply(tr::now),
	Data::Reactions::kFakeEffectId))
, _send(canSend()
	? std::make_unique<BottomRounded>(
		this,
		tr::lng_effect_send(tr::now),
		st::effectPreviewSend)
	: nullptr)
, _premiumPromoLabel(canSend()
	? nullptr
	: std::make_unique<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			tr::lng_effect_premium(
				lt_link,
				tr::lng_effect_premium_link() | Ui::Text::ToLink(),
				Ui::Text::WithEntities),
			st::effectPreviewPromoLabel),
		st::effectPreviewPromoPadding))
, _bottom(_send ? ((Ui::RpWidget*)_send.get()) : _premiumPromoLabel.get())
, _close(done)
, _actionWithEffect(ComposeActionWithEffect(action, _effectId, done)) {
	_chatStyle->apply(_theme.get());

	setupGeometry(position);
	setupItem();
	setupBackground();
	setupLottie();
	setupSend(details);

	toggle(true);
}

void EffectPreview::paintEvent(QPaintEvent *e) {
	checkIconBecameLoaded();

	const auto progress = _shownAnimation.value(_hiding ? 0. : 1.);
	if (!progress) {
		return;
	}

	auto p = QPainter(this);
	p.setOpacity(progress);
	p.drawImage(0, 0, _bg);

	if (!_bottomCache.isNull()) {
		p.drawPixmap(_bottom->pos(), _bottomCache);
	}

	if (!ready()) {
		paintLoading(p);
	} else {
		_loading = nullptr;
		p.drawImage(_iconRect, _icon);
		if (!_hiding) {
			p.setOpacity(1.);
		}
		paintLottie(p);
	}
}

bool EffectPreview::ready() const {
	return !_icon.isNull() && _lottie && _lottie->ready();
}

void EffectPreview::paintLoading(QPainter &p) {
	if (!_loading) {
		_loading = std::make_unique<Ui::InfiniteRadialAnimation>([=] {
			update();
		}, st::effectPreviewLoading);
		_loading->start(st::defaultInfiniteRadialAnimation.linearPeriod);
	}
	const auto loading = _iconRect.marginsRemoved(
		{ st::lineWidth, st::lineWidth, st::lineWidth, st::lineWidth });
	auto hq = PainterHighQualityEnabler(p);
	Ui::InfiniteRadialAnimation::Draw(
		p,
		_loading->computeState(),
		loading.topLeft(),
		loading.size(),
		width(),
		_chatStyle->msgInDateFg(),
		st::effectPreviewLoading.thickness);
}

void EffectPreview::paintLottie(QPainter &p) {
	const auto factor = style::DevicePixelRatio();
	auto request = Lottie::FrameRequest();
	request.box = _inner.size() * factor;
	const auto rightAligned = _item->hasRightLayout();
	if (!rightAligned) {
		request.mirrorHorizontal = true;
	}
	const auto frame = _lottie->frameInfo(request);
	p.drawImage(
		QRect(_inner.topLeft(), frame.image.size() / factor),
		frame.image);
	_lottie->markFrameShown();
}

void EffectPreview::hideAnimated() {
	toggle(false);
}

void EffectPreview::mousePressEvent(QMouseEvent *e) {
	hideAnimated();
}

void EffectPreview::setupGeometry(QPoint position) {
	const auto parent = parentWidget();
	const auto innerSize = HistoryView::Sticker::MessageEffectSize();
	const auto shadow = st::previewMenu.shadow;
	const auto extend = shadow.extend;
	_inner = QRect(QPoint(extend.left(), extend.top()), innerSize);
	_bottom->resizeToWidth(_inner.width());
	const auto size = _inner.marginsAdded(extend).size()
		+ QSize(0, _bottom->height());
	const auto left = std::max(
		std::min(
			position.x() - size.width() / 2,
			parent->width() - size.width()),
		0);
	const auto topMin = std::min((parent->height() - size.height()) / 2, 0);
	const auto top = std::max(
		std::min(
			position.y() - size.height() / 2,
			parent->height() - size.height()),
		topMin);
	setGeometry(left, top, size.width(), size.height());
	_bottom->setGeometry(
		_inner.x(),
		_inner.y() + _inner.height(),
		_inner.width(),
		_bottom->height());
}

void EffectPreview::setupBackground() {
	const auto ratio = style::DevicePixelRatio();
	_bg = QImage(
		size() * ratio,
		QImage::Format_ARGB32_Premultiplied);
	_bg.setDevicePixelRatio(ratio);
	repaintBackground();
	_theme->repaintBackgroundRequests() | rpl::start_with_next([=] {
		repaintBackground();
		update();
	}, lifetime());
}

void EffectPreview::setupItem() {
	_item->resizeGetHeight(st::windowMinWidth);

	const auto icon = _item->effectIconGeometry();
	Assert(!icon.isEmpty());

	const auto size = _inner.size();
	const auto shift = _item->hasRightLayout()
		? (-size.width() / 3)
		: (size.width() / 3);
	const auto position = QPoint(
		shift + icon.x() + (icon.width() - size.width()) / 2,
		icon.y() + (icon.height() - size.height()) / 2);
	_itemShift = _inner.topLeft() - position;
	_iconRect = icon.translated(_itemShift);
}

void EffectPreview::repaintBackground() {
	const auto ratio = style::DevicePixelRatio();
	const auto inner = _inner.size() + QSize(0, _bottom->height());
	auto bg = QImage(
		inner * ratio,
		QImage::Format_ARGB32_Premultiplied);
	bg.setDevicePixelRatio(ratio);

	{
		auto p = Painter(&bg);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(inner.width(), inner.height() * 5),
			QRect(QPoint(), inner));
		p.fillRect(
			QRect(0, _inner.height(), _inner.width(), _bottom->height()),
			st::previewMarkRead.bgColor);

		p.translate(_itemShift - _inner.topLeft());
		auto rect = QRect(0, 0, st::windowMinWidth, _inner.height());
		auto context = _theme->preparePaintContext(
			_chatStyle.get(),
			rect,
			rect,
			false);
		context.outbg = _item->hasOutLayout();
		_item->draw(p, context);
		p.translate(_inner.topLeft() - _itemShift);

		auto hq = PainterHighQualityEnabler(p);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		auto roundRect = Ui::RoundRect(st::previewMenu.radius, st::menuBg);
		roundRect.paint(p, QRect(QPoint(), inner), RectPart::AllCorners);
	}

	_bg.fill(Qt::transparent);
	auto p = QPainter(&_bg);

	const auto &shadow = st::previewMenu.animation.shadow;
	const auto shadowed = QRect(_inner.topLeft(), inner);
	Ui::Shadow::paint(p, shadowed, width(), shadow);
	p.drawImage(_inner.topLeft(), bg);
}

void EffectPreview::setupLottie() {
	const auto reactions = &_show->session().data().reactions();
	reactions->preloadEffectImageFor(_effectId);

	if (const auto document = _effect.aroundAnimation) {
		_media = document->createMediaView();
	} else {
		_media = _effect.selectAnimation->createMediaView();
	}
	rpl::single(rpl::empty) | rpl::then(
		_show->session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		if (checkLoaded()) {
			_readyCheckLifetime.destroy();
			createLottie();
		}
	}, _readyCheckLifetime);
}

void EffectPreview::createLottie() {
	_lottie = _show->session().emojiStickersPack().effectPlayer(
		_media->owner(),
		_bytes,
		_filepath,
		Stickers::EffectType::MessageEffect);
	const auto raw = _lottie.get();
	raw->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		v::match(update.data, [&](const Lottie::Information &information) {
		}, [&](const Lottie::DisplayFrameRequest &request) {
			this->update();
		});
	}, raw->lifetime());
}

bool EffectPreview::canSend() const {
	return !_effect.premium || _show->session().premium();
}

void EffectPreview::setupSend(Details details) {
	if (_send) {
		_send->setClickedCallback([=] {
			_actionWithEffect({}, details);
		});
		const auto type = details.type;
		SetupMenuAndShortcuts(_send.get(), _show, [=] {
			return Details{ .type = type };
		}, _actionWithEffect);
	} else {
		_premiumPromoLabel->entity()->setClickHandlerFilter([=](auto&&...) {
			const auto window = _show->resolveWindow();
			if (window) {
				if (const auto onstack = _close) {
					onstack();
				}
				ShowPremiumPreviewBox(window, PremiumFeature::Effects);
			}
			return false;
		});
	}
}

bool EffectPreview::checkIconBecameLoaded() {
	if (!_icon.isNull()) {
		return false;
	}
	const auto reactions = &_show->session().data().reactions();
	_icon = reactions->resolveEffectImageFor(_effect.id.custom());
	if (_icon.isNull()) {
		return false;
	}
	repaintBackground();
	return true;
}

bool EffectPreview::checkLoaded() {
	if (checkIconBecameLoaded()) {
		update();
	}
	if (_effect.aroundAnimation) {
		_bytes = _media->bytes();
		_filepath = _media->owner()->filepath();
	} else {
		_bytes = _media->videoThumbnailContent();
	}
	return !_icon.isNull() && (!_bytes.isEmpty() || !_filepath.isEmpty());
}

void EffectPreview::toggle(bool shown) {
	if (!shown && _hiding) {
		return;
	}
	_hiding = !shown;
	if (_bottomCache.isNull()) {
		_bottomCache = Ui::GrabWidget(_bottom);
		_bottom->hide();
	}
	_shownAnimation.start([=] {
		update();
		if (!_shownAnimation.animating()) {
			if (_hiding) {
				delete this;
			} else {
				_bottomCache = QPixmap();
				_bottom->show();
			}
		}
	}, shown ? 0. : 1., shown ? 1. : 0., kToggleDuration, anim::easeOutCirc);
	show();
}

} // namespace

Fn<void(Action, Details)> DefaultCallback(
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void(Api::SendOptions)> send) {
	const auto guard = Ui::MakeWeak(show->toastParent());
	return [=](Action action, Details details) {
		if (action.type == ActionType::Send) {
			send(action.options);
			return;
		}
		auto box = HistoryView::PrepareScheduleBox(
			guard,
			show,
			details,
			send,
			action.options);
		const auto weak = Ui::MakeWeak(box.data());
		show->showBox(std::move(box));
		if (const auto strong = weak.data()) {
			strong->setCloseByOutsideClick(false);
		}
	};
}

FillMenuResult AttachSendMenuEffect(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> show,
		Details details,
		Fn<void(Action, Details)> action,
		std::optional<QPoint> desiredPositionOverride) {
	Expects(show != nullptr);

	using namespace HistoryView::Reactions;
	const auto effect = std::make_shared<QPointer<EffectPreview>>();
	const auto position = desiredPositionOverride.value_or(QCursor::pos());
	const auto selector = (show && details.effectAllowed)
		? AttachSelectorToMenu(
			menu,
			position,
			st::reactPanelEmojiPan,
			show,
			LookupPossibleEffects(&show->session()),
			{ tr::lng_effect_add_title(tr::now) },
			nullptr, // iconFactory
			[=] { return (*effect) != nullptr; }) // paused
		: base::make_unexpected(AttachSelectorResult::Skipped);
	if (!selector) {
		if (selector.error() == AttachSelectorResult::Failed) {
			return FillMenuResult::Failed;
		}
		menu->prepareGeometryFor(position);
		return FillMenuResult::Prepared;
	}

	(*selector)->chosen(
	) | rpl::start_with_next([=](ChosenReaction chosen) {
		const auto &reactions = show->session().data().reactions();
		const auto &effects = reactions.list(Data::Reactions::Type::Effects);
		const auto i = ranges::find(effects, chosen.id, &Data::Reaction::id);
		if (i != end(effects)) {
			if (const auto strong = effect->data()) {
				strong->hideAnimated();
			}
			const auto weak = Ui::MakeWeak(menu);
			const auto done = [=] {
				delete effect->data();
				if (const auto strong = weak.data()) {
					strong->hideMenu(true);
				}
			};
			*effect = Ui::CreateChild<EffectPreview>(
				menu,
				show,
				details,
				menu->mapFromGlobal(chosen.globalGeometry.center()),
				*i,
				action,
				crl::guard(menu, done));
			(*effect)->show();
		}
	}, menu->lifetime());

	return FillMenuResult::Prepared;
}

FillMenuResult FillSendMenu(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<ChatHelpers::Show> showForEffect,
		Details details,
		Fn<void(Action, Details)> action,
		const style::ComposeIcons *iconsOverride,
		std::optional<QPoint> desiredPositionOverride) {
	const auto type = details.type;
	const auto sending = (type != Type::Disabled);
	const auto empty = !sending
		&& (details.spoiler == SpoilerState::None)
		&& (details.caption == CaptionState::None)
		&& !details.price.has_value();
	if (empty || !action) {
		return FillMenuResult::Skipped;
	}
	const auto &icons = iconsOverride
		? *iconsOverride
		: st::defaultComposeIcons;

	if (sending && type != Type::Reminder) {
		menu->addAction(
			tr::lng_send_silent_message(tr::now),
			[=] { action({ Api::SendOptions{ .silent = true } }, details); },
			&icons.menuMute);
	}
	if (sending && type != Type::SilentOnly) {
		menu->addAction(
			(type == Type::Reminder
				? tr::lng_reminder_message(tr::now)
				: tr::lng_schedule_message(tr::now)),
			[=] { action({ .type = ActionType::Schedule }, details); },
			&icons.menuSchedule);
	}
	if (sending && type == Type::ScheduledToUser) {
		menu->addAction(
			tr::lng_scheduled_send_until_online(tr::now),
			[=] { action(
				{ Api::DefaultSendWhenOnlineOptions() },
				details); },
			&icons.menuWhenOnline);
	}

	if ((type != Type::Disabled)
		&& ((details.spoiler != SpoilerState::None)
			|| (details.caption != CaptionState::None)
			|| details.price.has_value())) {
		menu->addSeparator(&st::expandedMenuSeparator);
	}
	if (details.spoiler != SpoilerState::None) {
		const auto spoilered = (details.spoiler == SpoilerState::Enabled);
		menu->addAction(
			(spoilered
				? tr::lng_context_disable_spoiler(tr::now)
				: tr::lng_context_spoiler_effect(tr::now)),
			[=] { action({ .type = spoilered
				? ActionType::SpoilerOff
				: ActionType::SpoilerOn
			}, details); },
			spoilered ? &icons.menuSpoilerOff : &icons.menuSpoiler);
	}
	if (details.caption != CaptionState::None) {
		const auto above = (details.caption == CaptionState::Above);
		menu->addAction(
			(above
				? tr::lng_caption_move_down(tr::now)
				: tr::lng_caption_move_up(tr::now)),
			[=] { action({ .type = above
				? ActionType::CaptionDown
				: ActionType::CaptionUp
			}, details); },
			above ? &icons.menuBelow : &icons.menuAbove);
	}
	if (details.price) {
		menu->addAction(
			((*details.price > 0)
				? tr::lng_context_change_price(tr::now)
				: tr::lng_context_make_paid(tr::now)),
			[=] { action({ .type = ActionType::ChangePrice }, details); },
			&icons.menuPrice);
	}

	if (showForEffect) {
		return AttachSendMenuEffect(
			menu,
			showForEffect,
			details,
			action,
			desiredPositionOverride);
	}
	const auto position = desiredPositionOverride.value_or(QCursor::pos());
	menu->prepareGeometryFor(position);
	return FillMenuResult::Prepared;
}

void SetupMenuAndShortcuts(
		not_null<Ui::RpWidget*> button,
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<Details()> details,
		Fn<void(Action, Details)> action) {
	const auto menu = std::make_shared<base::unique_qptr<Ui::PopupMenu>>();
	const auto showMenu = [=] {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		const auto result = FillSendMenu(*menu, show, details(), action);
		if (result != FillMenuResult::Prepared) {
			return false;
		}
		(*menu)->popupPrepared();
		return true;
	};
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu && showMenu()) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	Shortcuts::Requests(
	) | rpl::filter([=] {
		return button->isActiveWindow();
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;

		const auto now = details().type;
		if (now == Type::Disabled) {
			return;
		}
		((now != Type::Reminder)
			&& request->check(Command::SendSilentMessage)
			&& request->handle([=] {
				action({ Api::SendOptions{ .silent = true } }, details());
				return true;
			}))
		||
		((now != Type::SilentOnly)
			&& request->check(Command::ScheduleMessage)
			&& request->handle([=] {
				action({ .type = ActionType::Schedule }, details());
				return true;
			}))
		||
		(request->check(Command::JustSendMessage) && request->handle([=] {
			const auto post = [&](QEvent::Type type) {
				QApplication::postEvent(
					button,
					new QMouseEvent(
						type,
						QPointF(0, 0),
						Qt::LeftButton,
						Qt::LeftButton,
						Qt::NoModifier));
			};
			post(QEvent::MouseButtonPress);
			post(QEvent::MouseButtonRelease);
			return true;
		}));
	}, button->lifetime());
}

void SetupReadAllMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread,
		const QString &text,
		Fn<void(not_null<Data::Thread*>, Fn<void()>)> sendReadRequest) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
		base::flat_set<base::weak_ptr<Data::Thread>> sentForEntries;
	};
	const auto state = std::make_shared<State>();
	const auto showMenu = [=] {
		const auto thread = base::make_weak(currentThread());
		if (!thread) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		state->menu->addAction(text, [=] {
			const auto strong = thread.get();
			if (!strong || !state->sentForEntries.emplace(thread).second) {
				return;
			}
			sendReadRequest(strong, [=] {
				state->sentForEntries.remove(thread);
			});
		}, &st::menuIconMarkRead);
		state->menu->popup(QCursor::pos());
	};

	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::ContextMenu) {
			showMenu();
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});
}

void SetupUnreadMentionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread) {
	const auto text = tr::lng_context_mark_read_mentions_all(tr::now);
	const auto sendOne = [=](
			base::weak_ptr<Data::Thread> weakThread,
			Fn<void()> done,
			auto resend) -> void {
		const auto thread = weakThread.get();
		if (!thread) {
			done();
			return;
		}
		const auto peer = thread->peer();
		const auto topic = thread->asTopic();
		const auto rootId = topic ? topic->rootId() : 0;
		using Flag = MTPmessages_ReadMentions::Flag;
		peer->session().api().request(MTPmessages_ReadMentions(
			MTP_flags(rootId ? Flag::f_top_msg_id : Flag()),
			peer->input,
			MTP_int(rootId)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = peer->session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				resend(weakThread, done, resend);
			} else {
				done();
				peer->owner().history(peer)->clearUnreadMentionsFor(rootId);
			}
		}).fail(done).send();
	};
	const auto sendRequest = [=](
			not_null<Data::Thread*> thread,
			Fn<void()> done) {
		sendOne(base::make_weak(thread), std::move(done), sendOne);
	};
	SetupReadAllMenu(button, currentThread, text, sendRequest);
}

void SetupUnreadReactionsMenu(
		not_null<Ui::RpWidget*> button,
		Fn<Data::Thread*()> currentThread) {
	const auto text = tr::lng_context_mark_read_reactions_all(tr::now);
	const auto sendOne = [=](
			base::weak_ptr<Data::Thread> weakThread,
			Fn<void()> done,
			auto resend) -> void {
		const auto thread = weakThread.get();
		if (!thread) {
			done();
			return;
		}
		const auto topic = thread->asTopic();
		const auto peer = thread->peer();
		const auto rootId = topic ? topic->rootId() : 0;
		using Flag = MTPmessages_ReadReactions::Flag;
		peer->session().api().request(MTPmessages_ReadReactions(
			MTP_flags(rootId ? Flag::f_top_msg_id : Flag(0)),
			peer->input,
			MTP_int(rootId)
		)).done([=](const MTPmessages_AffectedHistory &result) {
			const auto offset = peer->session().api().applyAffectedHistory(
				peer,
				result);
			if (offset > 0) {
				resend(weakThread, done, resend);
			} else {
				done();
				peer->owner().history(peer)->clearUnreadReactionsFor(rootId);
			}
		}).fail(done).send();
	};
	const auto sendRequest = [=](
			not_null<Data::Thread*> thread,
			Fn<void()> done) {
		sendOne(base::make_weak(thread), std::move(done), sendOne);
	};
	SetupReadAllMenu(button, currentThread, text, sendRequest);
}

} // namespace SendMenu

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/reactions_settings_box.h"

#include "base/unixtime.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/history_view_element.h"
#include "history/view/reactions/history_view_reactions_strip.h"
#include "lang/lang_keys.h"
#include "boxes/premium_preview_box.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "settings/settings_premium.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/scroll_content_shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/toasts/common_toasts.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/animated_icon.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_settings.h"

namespace {

PeerId GenerateUser(not_null<History*> history, const QString &name) {
	Expects(history->peer->isUser());

	const auto peerId = Data::FakePeerIdForJustName(name);
	history->owner().processUser(MTP_user(
		MTP_flags(MTPDuser::Flag::f_first_name | MTPDuser::Flag::f_min),
		peerToBareMTPInt(peerId),
		MTP_long(0),
		MTP_string(tr::lng_settings_chat_message_reply_from(tr::now)),
		MTPstring(), // last name
		MTPstring(), // username
		MTPstring(), // phone
		MTPUserProfilePhoto(), // profile photo
		MTPUserStatus(), // status
		MTP_int(0), // bot info version
		MTPVector<MTPRestrictionReason>(), // restrictions
		MTPstring(), // bot placeholder
		MTPstring(), // lang code
		MTPEmojiStatus()));
	return peerId;
}

AdminLog::OwnedItem GenerateItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		PeerId from,
		MsgId replyTo,
		const QString &text) {
	Expects(history->peer->isUser());

	const auto item = history->addNewLocalMessage(
		history->nextNonHistoryEntryId(),
		MessageFlag::FakeHistoryItem
			| MessageFlag::HasFromId
			| MessageFlag::HasReplyInfo,
		UserId(), // via
		replyTo,
		base::unixtime::now(), // date
		from,
		QString(), // postAuthor
		TextWithEntities{ .text = text },
		MTP_messageMediaEmpty(),
		HistoryMessageMarkupData(),
		uint64(0)); // groupedId

	return AdminLog::OwnedItem(delegate, item);
}

void AddMessage(
		not_null<Ui::VerticalLayout*> container,
		not_null<Window::SessionController*> controller,
		rpl::producer<Data::ReactionId> &&idValue,
		int width) {

	const auto widget = container->add(
		object_ptr<Ui::RpWidget>(container),
		style::margins(
			0,
			st::settingsSectionSkip,
			0,
			st::settingsPrivacySkipTop));

	class Delegate final : public HistoryView::SimpleElementDelegate {
	public:
		using HistoryView::SimpleElementDelegate::SimpleElementDelegate;
	private:
		HistoryView::Context elementContext() override {
			return HistoryView::Context::ContactPreview;
		}
	};

	struct State {
		AdminLog::OwnedItem reply;
		AdminLog::OwnedItem item;
		std::unique_ptr<Delegate> delegate;
		std::unique_ptr<Ui::ChatStyle> style;

		struct {
			std::vector<rpl::lifetime> lifetimes;
			bool flag = false;
		} icons;
	};
	const auto state = container->lifetime().make_state<State>();
	state->delegate = std::make_unique<Delegate>(
		controller,
		crl::guard(widget, [=] { widget->update(); }));
	state->style = std::make_unique<Ui::ChatStyle>();
	state->style->apply(controller->defaultChatTheme().get());
	state->icons.lifetimes = std::vector<rpl::lifetime>(2);

	const auto history = controller->session().data().history(
		PeerData::kServiceNotificationsId);
	state->reply = GenerateItem(
		state->delegate.get(),
		history,
		GenerateUser(
			history,
			tr::lng_settings_chat_message_reply_from(tr::now)),
		0,
		tr::lng_settings_chat_message_reply(tr::now));
	auto message = GenerateItem(
		state->delegate.get(),
		history,
		history->peer->id,
		state->reply->data()->fullId().msg,
		tr::lng_settings_chat_message(tr::now));
	const auto view = message.get();
	state->item = std::move(message);

	const auto padding = st::settingsForwardPrivacyPadding;

	const auto updateWidgetSize = [=](int width) {
		const auto height = view->resizeGetHeight(width);
		const auto top = view->marginTop();
		const auto bottom = view->marginBottom();
		const auto full = padding + top + height + bottom + padding;
		widget->resize(width, full);
	};
	widget->widthValue(
	) | rpl::filter(
		rpl::mappers::_1 >= (st::historyMinimalWidth / 2)
	) | rpl::start_with_next(updateWidgetSize, widget->lifetime());
	updateWidgetSize(width);

	const auto rightSize = st::settingsReactionCornerSize;
	const auto rightRect = [=] {
		const auto viewInner = view->innerGeometry();
		return QRect(
			viewInner.x() + viewInner.width(),
			padding
				+ view->marginTop()
				+ view->resizeGetHeight(widget->width())
				- rightSize.height(),
			rightSize.width(),
			rightSize.height()).translated(st::settingsReactionCornerSkip);
	};

	widget->paintRequest(
	) | rpl::start_with_next([=](const QRect &rect) {
		Window::SectionWidget::PaintBackground(
			controller,
			controller->defaultChatTheme().get(), // #TODO themes
			widget,
			rect);

		Painter p(widget);
		auto hq = PainterHighQualityEnabler(p);
		const auto theme = controller->defaultChatTheme().get();
		auto context = theme->preparePaintContext(
			state->style.get(),
			widget->rect(),
			widget->rect(),
			controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer));
		context.outbg = view->hasOutLayout();

		{
			const auto radius = rightSize.height() / 2;
			const auto r = rightRect();
			const auto &st = context.st->messageStyle(
				context.outbg,
				context.selected());
			p.setPen(Qt::NoPen);
			p.setBrush(st.msgShadow);
			p.drawRoundedRect(r.translated(0, st::msgShadow), radius, radius);
			p.setBrush(st.msgBg);
			p.drawRoundedRect(r, radius, radius);
		}

		p.translate(padding / 2, padding + view->marginBottom());
		view->draw(p, context);
	}, widget->lifetime());

	auto selectedId = rpl::duplicate(idValue);
	std::move(
		selectedId
	) | rpl::start_with_next([
		=,
		idValue = std::move(idValue),
		iconSize = st::settingsReactionMessageSize
	](const Data::ReactionId &id) {
		const auto index = state->icons.flag ? 1 : 0;
		state->icons.flag = !state->icons.flag;
		state->icons.lifetimes[index] = rpl::lifetime();
		const auto &reactions = controller->session().data().reactions();
		auto iconPositionValue = widget->geometryValue(
		) | rpl::map([=](const QRect &r) {
			return widget->pos()
				+ rightRect().topLeft()
				+ QPoint(
					(rightSize.width() - iconSize) / 2,
					(rightSize.height() - iconSize) / 2);
		});
		auto destroys = rpl::duplicate(
			idValue
		) | rpl::skip(1) | rpl::to_empty;
		if (const auto customId = id.custom()) {
			AddReactionCustomIcon(
				container,
				std::move(iconPositionValue),
				iconSize,
				controller,
				customId,
				std::move(destroys),
				&state->icons.lifetimes[index]);
			return;
		}
		for (const auto &r : reactions.list(Data::Reactions::Type::Active)) {
			if (r.id != id) {
				continue;
			}
			AddReactionAnimatedIcon(
				container,
				std::move(iconPositionValue),
				iconSize,
				r,
				rpl::never<>(),
				std::move(destroys),
				&state->icons.lifetimes[index]);
			return;
		}
	}, widget->lifetime());
}

not_null<Ui::RpWidget*> AddReactionIconWrap(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QPoint> iconPositionValue,
		int iconSize,
		Fn<void(not_null<QWidget*>, QPainter&)> paintCallback,
		rpl::producer<> &&destroys,
		not_null<rpl::lifetime*> stateLifetime) {
	struct State {
		base::unique_qptr<Ui::RpWidget> widget;
		Ui::Animations::Simple finalAnimation;
	};

	const auto state = stateLifetime->make_state<State>();
	state->widget = base::make_unique_q<Ui::RpWidget>(parent);

	const auto widget = state->widget.get();
	widget->resize(iconSize, iconSize);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);

	std::move(
		iconPositionValue
	) | rpl::start_with_next([=](const QPoint &point) {
		widget->moveToLeft(point.x(), point.y());
	}, widget->lifetime());

	const auto update = crl::guard(widget, [=] { widget->update(); });

	widget->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(widget);

		if (state->finalAnimation.animating()) {
			const auto progress = 1. - state->finalAnimation.value(0.);
			const auto size = widget->size();
			const auto scaledSize = size * progress;
			const auto scaledCenter = QPoint(
				(size.width() - scaledSize.width()) / 2.,
				(size.height() - scaledSize.height()) / 2.);
			p.setOpacity(progress);
			p.translate(scaledCenter);
			p.scale(progress, progress);
		}

		paintCallback(widget, p);
	}, widget->lifetime());

	std::move(
		destroys
	) | rpl::take(1) | rpl::start_with_next([=, from = 0., to = 1.] {
		state->finalAnimation.start(
			[=](float64 value) {
				update();
				if (value == to) {
					stateLifetime->destroy();
				}
			},
			from,
			to,
			st::defaultPopupMenu.showDuration);
	}, widget->lifetime());

	widget->raise();
	widget->show();

	return widget;
}

} // namespace

void AddReactionAnimatedIcon(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QPoint> iconPositionValue,
		int iconSize,
		const Data::Reaction &reaction,
		rpl::producer<> &&selects,
		rpl::producer<> &&destroys,
		not_null<rpl::lifetime*> stateLifetime) {
	struct State {
		struct Entry {
			std::shared_ptr<Data::DocumentMedia> media;
			std::shared_ptr<Ui::AnimatedIcon> icon;
		};
		Entry appear;
		Entry select;
		bool appearAnimated = false;
		rpl::lifetime loadingLifetime;
	};
	const auto state = stateLifetime->make_state<State>();

	state->appear.media = reaction.appearAnimation->createMediaView();
	state->select.media = reaction.selectAnimation->createMediaView();
	state->appear.media->checkStickerLarge();
	state->select.media->checkStickerLarge();
	rpl::single() | rpl::then(
		reaction.appearAnimation->session().downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		const auto check = [&](State::Entry &entry) {
			if (!entry.media) {
				return true;
			} else if (!entry.media->loaded()) {
				return false;
			}
			entry.icon = HistoryView::Reactions::DefaultIconFactory(
				entry.media.get(),
				iconSize);
			entry.media = nullptr;
			return true;
		};
		if (check(state->select) && check(state->appear)) {
			state->loadingLifetime.destroy();
		}
	}, state->loadingLifetime);

	const auto paintCallback = [=](not_null<QWidget*> widget, QPainter &p) {
		const auto paintFrame = [&](not_null<Ui::AnimatedIcon*> animation) {
			const auto frame = animation->frame();
			p.drawImage(
				QRect(
					(widget->width() - iconSize) / 2,
					(widget->height() - iconSize) / 2,
					iconSize,
					iconSize),
				frame);
		};

		const auto appear = state->appear.icon.get();
		if (appear && !state->appearAnimated) {
			state->appearAnimated = true;
			appear->animate(crl::guard(widget, [=] { widget->update(); }));
		}
		if (appear && appear->animating()) {
			paintFrame(appear);
		} else if (const auto select = state->select.icon.get()) {
			paintFrame(select);
		}

	};
	const auto widget = AddReactionIconWrap(
		parent,
		std::move(iconPositionValue),
		iconSize,
		paintCallback,
		std::move(destroys),
		stateLifetime);

	std::move(
		selects
	) | rpl::start_with_next([=] {
		const auto select = state->select.icon.get();
		if (select && !select->animating()) {
			select->animate(crl::guard(widget, [=] { widget->update(); }));
		}
	}, widget->lifetime());
}

void AddReactionCustomIcon(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QPoint> iconPositionValue,
		int iconSize,
		not_null<Window::SessionController*> controller,
		DocumentId customId,
		rpl::producer<> &&destroys,
		not_null<rpl::lifetime*> stateLifetime) {
	struct State {
		std::unique_ptr<Ui::Text::CustomEmoji> custom;
		Fn<void()> repaint;
	};
	const auto state = stateLifetime->make_state<State>();
	static constexpr auto tag = Data::CustomEmojiManager::SizeTag::Normal;
	state->custom = controller->session().data().customEmojiManager().create(
		customId,
		[=] { state->repaint(); },
		tag);

	const auto paintCallback = [=](not_null<QWidget*> widget, QPainter &p) {
		const auto ratio = style::DevicePixelRatio();
		const auto size = Data::FrameSizeFromTag(tag) / ratio;
		state->custom->paint(p, {
			.preview = st::windowBgRipple->c,
			.now = crl::now(),
			.position = QPoint(
				(widget->width() - size) / 2,
				(widget->height() - size) / 2),
			.paused = controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer),
		});
	};
	const auto widget = AddReactionIconWrap(
		parent,
		std::move(iconPositionValue),
		iconSize,
		paintCallback,
		std::move(destroys),
		stateLifetime);
	state->repaint = crl::guard(widget, [=] { widget->update(); });
}

void ReactionsSettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {

	struct State {
		rpl::variable<Data::ReactionId> selectedId;
	};

	const auto &reactions = controller->session().data().reactions();
	const auto state = box->lifetime().make_state<State>();
	state->selectedId = reactions.favoriteId();

	const auto pinnedToTop = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));

	auto idValue = state->selectedId.value();
	AddMessage(pinnedToTop, controller, std::move(idValue), box->width());

	Settings::AddSubsectionTitle(
		pinnedToTop,
		tr::lng_settings_chat_reactions_subtitle());

	const auto container = box->verticalLayout();

	const auto check = Ui::CreateChild<Ui::RpWidget>(container.get());
	check->resize(st::settingsReactionCornerSize);
	check->setAttribute(Qt::WA_TransparentForMouseEvents);
	check->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(check);
		st::mediaPlayerMenuCheck.paintInCenter(p, check->rect());
	}, check->lifetime());
	const auto checkButton = [=](not_null<const Ui::RpWidget*> button) {
		check->moveToRight(
			st::settingsButtonRightSkip,
			button->y() + (button->height() - check->height()) / 2);
	};

	auto firstCheckedButton = (Ui::RpWidget*)(nullptr);
	const auto premiumPossible = controller->session().premiumPossible();
	auto list = reactions.list(Data::Reactions::Type::Active);
	if (const auto favorite = reactions.favorite()) {
		if (favorite->id.custom()) {
			list.insert(begin(list), *favorite);
		}
	}
	for (const auto &r : list) {
		const auto button = Settings::AddButton(
			container,
			rpl::single<QString>(base::duplicate(r.title)),
			st::settingsButton);

		const auto premium = r.premium;
		if (premium && !premiumPossible) {
			continue;
		}

		const auto iconSize = st::settingsReactionSize;
		const auto left = button->st().iconLeft;
		auto iconPositionValue = button->sizeValue(
		) | rpl::map([=](const QSize &s) {
			return QPoint(
				left + st::settingsReactionRightSkip,
				(s.height() - iconSize) / 2);
		});
		if (const auto customId = r.id.custom()) {
			AddReactionCustomIcon(
				button,
				std::move(iconPositionValue),
				iconSize,
				controller,
				customId,
				rpl::never<>(),
				&button->lifetime());
		} else {
			AddReactionAnimatedIcon(
				button,
				std::move(iconPositionValue),
				iconSize,
				r,
				button->events(
				) | rpl::filter([=](not_null<QEvent*> event) {
					return event->type() == QEvent::Enter;
				}) | rpl::to_empty,
				rpl::never<>(),
				&button->lifetime());
		}
		button->setClickedCallback([=, id = r.id] {
			if (premium && !controller->session().premium()) {
				ShowPremiumPreviewBox(
					controller,
					PremiumPreview::InfiniteReactions);
				return;
			}
			checkButton(button);
			state->selectedId = id;
		});
		if (r.id == state->selectedId.current()) {
			firstCheckedButton = button;
		}
	}
	if (firstCheckedButton) {
		firstCheckedButton->geometryValue(
		) | rpl::filter([=](const QRect &r) {
			return r.isValid();
		}) | rpl::take(1) | rpl::start_with_next([=] {
			checkButton(firstCheckedButton);
		}, firstCheckedButton->lifetime());
	}
	check->raise();

	box->setTitle(tr::lng_settings_chat_reactions_title());
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_settings_save(), [=] {
		const auto &data = controller->session().data();
		const auto selectedId = state->selectedId.current();
		if (data.reactions().favoriteId() != selectedId) {
			data.reactions().setFavorite(selectedId);
		}
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

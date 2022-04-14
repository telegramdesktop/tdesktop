/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/reactions_settings_box.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/history.h"
#include "history/history_message.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_react_button.h" // DefaultIconFactory
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/scroll_content_shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_settings.h"

namespace {

constexpr auto kVisibleButtonsCount = 7;

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
		MTPstring())); // lang code
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
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> &&emojiValue) {

	const auto widget = box->addRow(
		object_ptr<Ui::RpWidget>(box),
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
	const auto state = box->lifetime().make_state<State>();
	state->delegate = std::make_unique<Delegate>(
		controller,
		crl::guard(widget, [=] { widget->update(); }));
	state->style = std::make_unique<Ui::ChatStyle>();
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

	widget->widthValue(
	) | rpl::filter(
		rpl::mappers::_1 >= (st::historyMinimalWidth / 2)
	) | rpl::start_with_next([=](int width) {
		const auto height = view->resizeGetHeight(width);
		const auto top = view->marginTop();
		const auto bottom = view->marginBottom();
		const auto full = padding + top + height + bottom + padding;
		widget->resize(width, full);
	}, widget->lifetime());

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
			widget->rect());
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

	auto selectedEmoji = rpl::duplicate(emojiValue);
	std::move(
		selectedEmoji
	) | rpl::start_with_next([
		=,
		emojiValue = std::move(emojiValue),
		iconSize = st::settingsReactionMessageSize
	](const QString &emoji) {
		const auto &reactions = controller->session().data().reactions();
		for (const auto &r : reactions.list(Data::Reactions::Type::All)) {
			if (emoji != r.emoji) {
				continue;
			}
			const auto index = state->icons.flag ? 1 : 0;
			state->icons.lifetimes[index] = rpl::lifetime();
			AddReactionLottieIcon(
				box->verticalLayout(),
				widget->geometryValue(
				) | rpl::map([=](const QRect &r) {
					return widget->pos()
						+ rightRect().topLeft()
						+ QPoint(
							(rightSize.width() - iconSize) / 2,
							(rightSize.height() - iconSize) / 2);
				}),
				iconSize,
				&controller->session(),
				r,
				rpl::never<>(),
				rpl::duplicate(emojiValue) | rpl::skip(1) | rpl::to_empty,
				&state->icons.lifetimes[index]);
			state->icons.flag = !state->icons.flag;
			return;
		}
	}, widget->lifetime());
}

} // namespace

void AddReactionLottieIcon(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QPoint> iconPositionValue,
		int iconSize,
		not_null<Main::Session*> session,
		const Data::Reaction &reaction,
		rpl::producer<> &&selects,
		rpl::producer<> &&destroys,
		not_null<rpl::lifetime*> stateLifetime) {

	struct State {
		struct Entry {
			std::shared_ptr<Data::DocumentMedia> media;
			std::shared_ptr<Lottie::Icon> icon;
		};
		Entry appear;
		Entry select;
		bool appearAnimated = false;
		rpl::lifetime loadingLifetime;

		base::unique_qptr<Ui::RpWidget> widget;

		Ui::Animations::Simple finalAnimation;
	};

	const auto state = stateLifetime->make_state<State>();
	state->widget = base::make_unique_q<Ui::RpWidget>(parent);

	state->appear.media = reaction.appearAnimation->createMediaView();
	state->select.media = reaction.selectAnimation->createMediaView();
	state->appear.media->checkStickerLarge();
	state->select.media->checkStickerLarge();
	rpl::single() | rpl::then(
		session->downloaderTaskFinished()
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
		Painter p(widget);

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

		const auto paintFrame = [&](not_null<Lottie::Icon*> animation) {
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
			appear->animate(update, 0, appear->framesCount() - 1);
		}
		if (appear && appear->animating()) {
			paintFrame(appear);
		} else if (const auto select = state->select.icon.get()) {
			paintFrame(select);
		}
	}, widget->lifetime());

	std::move(
		selects
	) | rpl::start_with_next([=] {
		const auto select = state->select.icon.get();
		if (select && !select->animating()) {
			select->animate(update, 0, select->framesCount() - 1);
		}
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
}

void ReactionsSettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {

	struct State {
		rpl::variable<QString> selectedEmoji;
	};

	const auto &reactions = controller->session().data().reactions();
	const auto state = box->lifetime().make_state<State>();
	state->selectedEmoji = reactions.favorite();

	AddMessage(box, controller, state->selectedEmoji.value());

	const auto container = box->verticalLayout();
	Settings::AddSubsectionTitle(
		container,
		tr::lng_settings_chat_reactions_subtitle());

	const auto &stButton = st::settingsButton;
	const auto scrollContainer = box->addRow(
		object_ptr<Ui::FixedHeightWidget>(
			box,
			kVisibleButtonsCount
				* (stButton.height
					+ stButton.padding.top()
					+ stButton.padding.bottom())),
		style::margins());
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		scrollContainer,
		st::boxScroll);
	const auto buttonsContainer = scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(scroll));
	scrollContainer->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		scroll->resize(s.width(), s.height());
		buttonsContainer->resizeToWidth(s.width());
	}, scroll->lifetime());

	const auto check = Ui::CreateChild<Ui::RpWidget>(buttonsContainer.data());
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
	for (const auto &r : reactions.list(Data::Reactions::Type::All)) {
		const auto button = Settings::AddButton(
			buttonsContainer,
			rpl::single<QString>(base::duplicate(r.title)),
			stButton);

		const auto iconSize = st::settingsReactionSize;
		AddReactionLottieIcon(
			button,
			button->sizeValue(
			) | rpl::map([=, left = button->st().iconLeft](const QSize &s) {
				return QPoint(
					left + st::settingsReactionRightSkip,
					(s.height() - iconSize) / 2);
			}),
			iconSize,
			&controller->session(),
			r,
			button->events(
			) | rpl::filter([=](not_null<QEvent*> event) {
				return event->type() == QEvent::Enter;
			}) | rpl::to_empty,
			rpl::never<>(),
			&button->lifetime());

		button->setClickedCallback([=, emoji = r.emoji] {
			checkButton(button);
			state->selectedEmoji = emoji;
		});
		if (r.emoji == state->selectedEmoji.current()) {
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

	Ui::SetupShadowsToScrollContent(
		scrollContainer,
		scroll,
		buttonsContainer->heightValue());

	box->setTitle(tr::lng_settings_chat_reactions_title());
	box->setWidth(st::boxWideWidth);
	box->addButton(tr::lng_settings_save(), [=] {
		const auto &data = controller->session().data();
		const auto selectedEmoji = state->selectedEmoji.current();
		if (data.reactions().favorite() != selectedEmoji) {
			data.reactions().setFavorite(selectedEmoji);
		}
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

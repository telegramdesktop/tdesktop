/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_blocked_peers.h"

#include "api/api_blocked_peers.h"
#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/settings_privacy_controllers.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Settings {

Blocked::Blocked(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _container(Ui::CreateChild<Ui::VerticalLayout>(this)) {

	setupContent();

	{
		auto padding = st::changePhoneIconPadding;
		padding.setBottom(padding.top());
		_loading = base::make_unique_q<Ui::CenterWrap<>>(
			this,
			object_ptr<Ui::PaddingWrap<>>(
				this,
				object_ptr<Ui::FlatLabel>(
					this,
					tr::lng_contacts_loading(),
					st::changePhoneDescription),
				std::move(padding)));
		Ui::ResizeFitChild(
			this,
			_loading.get(),
			st::settingsBlockedHeightMin);
	}

	_controller->session().api().blockedPeers().slice(
	) | rpl::start_with_next([=](const Api::BlockedPeers::Slice &slice) {
		checkTotal(slice.total);
	}, lifetime());

	_controller->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		if (update.peer->isBlocked()) {
			checkTotal(1);
		}
	}, lifetime());
}

rpl::producer<QString> Blocked::title() {
	return tr::lng_settings_blocked_users();
}

QPointer<Ui::RpWidget> Blocked::createPinnedToTop(not_null<QWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	Ui::AddSkip(content);

	AddButtonWithIcon(
		content,
		tr::lng_blocked_list_add(),
		st::settingsButtonActive,
		{ &st::menuIconBlockSettings }
	)->addClickHandler([=] {
		BlockedBoxController::BlockNewPeer(_controller);
	});

	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_blocked_list_about());

	{
		const auto subtitle = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
		Ui::AddSkip(subtitle->entity());
		auto subtitleText = _countBlocked.value(
		) | rpl::map([=](int count) {
			return tr::lng_blocked_list_subtitle(tr::now, lt_count, count);
		});
		Ui::AddSubsectionTitle(
			subtitle->entity(),
			rpl::duplicate(subtitleText),
			st::settingsBlockedListSubtitleAddPadding);
		subtitle->toggleOn(
			rpl::merge(
				_emptinessChanges.events() | rpl::map(!rpl::mappers::_1),
				_countBlocked.value() | rpl::map(rpl::mappers::_1 > 0)
			) | rpl::distinct_until_changed());

		// Workaround.
		std::move(
			subtitleText
		) | rpl::start_with_next([=] {
			subtitle->entity()->resizeToWidth(content->width());
		}, subtitle->lifetime());
	}

	return Ui::MakeWeak(not_null<Ui::RpWidget*>{ content });
}

void Blocked::setupContent() {
	using namespace rpl::mappers;

	const auto listWrap = _container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_container,
			object_ptr<Ui::VerticalLayout>(_container)));
	listWrap->toggleOn(
		_emptinessChanges.events_starting_with(true) | rpl::map(!_1),
		anim::type::instant);

	{
		struct State {
			std::unique_ptr<BlockedBoxController> controller;
			std::unique_ptr<PeerListContentDelegateSimple> delegate;
		};

		auto controller = std::make_unique<BlockedBoxController>(_controller);
		controller->setStyleOverrides(&st::settingsBlockedList);
		const auto content = listWrap->entity()->add(
			object_ptr<PeerListContent>(this, controller.get()));

		const auto state = content->lifetime().make_state<State>();
		state->controller = std::move(controller);
		state->delegate = std::make_unique<PeerListContentDelegateSimple>();

		state->delegate->setContent(content);
		state->controller->setDelegate(state->delegate.get());

		state->controller->rowsCountChanges(
		) | rpl::start_with_next([=](int total) {
			_countBlocked = total;
			checkTotal(total);
		}, content->lifetime());
		_countBlocked = content->fullRowsCount();
	}

	const auto emptyWrap = _container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_container,
			object_ptr<Ui::VerticalLayout>(_container)));
	emptyWrap->toggleOn(
		_emptinessChanges.events_starting_with(false),
		anim::type::instant);

	{
		const auto content = emptyWrap->entity();
		auto icon = CreateLottieIcon(
			content,
			{
				.name = u"blocked_peers_empty"_q,
				.sizeOverride = {
					st::changePhoneIconSize,
					st::changePhoneIconSize,
				},
			},
			st::settingsBlockedListIconPadding);
		content->add(std::move(icon.widget));

		_showFinished.events(
		) | rpl::start_with_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, content->lifetime());

		content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::FlatLabel>(
					content,
					tr::lng_blocked_list_empty_title(),
					st::changePhoneTitle)),
			st::changePhoneTitlePadding);

		content->add(
			object_ptr<Ui::CenterWrap<>>(
				content,
				object_ptr<Ui::FlatLabel>(
					content,
					tr::lng_blocked_list_empty_description(),
					st::changePhoneDescription)),
			st::changePhoneDescriptionPadding);

		Ui::AddSkip(content, st::settingsBlockedListIconPadding.top());
	}

	// We want minimal height to be the same no matter if subtitle
	// is visible or not, so minimal height isn't a constant here.
//	Ui::ResizeFitChild(this, _container, st::settingsBlockedHeightMin);

	widthValue(
	) | rpl::start_with_next([=](int width) {
		_container->resizeToWidth(width);
	}, _container->lifetime());

	rpl::combine(
		_container->heightValue(),
		_emptinessChanges.events_starting_with(true)
	) | rpl::start_with_next([=](int height, bool empty) {
		const auto subtitled = !empty || (_countBlocked.current() > 0);
		const auto total = st::settingsBlockedHeightMin;
		const auto padding = st::defaultSubsectionTitlePadding
			+ st::settingsBlockedListSubtitleAddPadding;
		const auto subtitle = st::defaultVerticalListSkip
			+ padding.top()
			+ st::defaultSubsectionTitle.style.font->height
			+ padding.bottom();
		const auto min = total - (subtitled ? subtitle : 0);
		resize(width(), std::max(height, min));
	}, _container->lifetime());
}

void Blocked::checkTotal(int total) {
	_loading = nullptr;
	_emptinessChanges.fire(total <= 0);
}

void Blocked::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	setChildVisibleTopBottom(_container, visibleTop, visibleBottom);
}

void Blocked::showFinished() {
	_showFinished.fire({});
}

} // namespace Settings

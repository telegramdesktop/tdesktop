/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_blocked_peers.h"

#include "settings/settings_common_session.h"

#include "api/api_blocked_peers.h"
#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
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
namespace {

using namespace Builder;

void BuildBlockedSection(SectionBuilder &builder) {
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"blocked/block-user"_q,
			.title = tr::lng_blocked_list_add(tr::now),
			.keywords = { u"block"_q, u"ban"_q, u"add"_q },
		};
	});
}

class Blocked : public Section<Blocked> {
public:
	Blocked(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void showFinished() override;

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

private:
	void setupContent();
	void checkTotal(int total);

	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

	const not_null<Ui::VerticalLayout*> _container;

	base::unique_qptr<Ui::RpWidget> _loading;

	rpl::variable<int> _countBlocked;

	rpl::event_stream<> _showFinished;
	rpl::event_stream<bool> _emptinessChanges;

	QPointer<Ui::RpWidget> _blockUserButton;

};

Blocked::Blocked(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _container(Ui::CreateChild<Ui::VerticalLayout>(this)) {

	setupContent();

	{
		auto padding = st::changePhoneIconPadding;
		padding.setBottom(padding.top());
		_loading = base::make_unique_q<Ui::PaddingWrap<>>(
			this,
			object_ptr<Ui::FlatLabel>(
				this,
				tr::lng_contacts_loading(),
				st::changePhoneDescription),
			std::move(padding));
		Ui::ResizeFitChild(
			this,
			_loading.get(),
			st::settingsBlockedHeightMin);
	}

	controller->session().api().blockedPeers().slice(
	) | rpl::on_next([=](const Api::BlockedPeers::Slice &slice) {
		checkTotal(slice.total);
	}, lifetime());

	controller->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		if (update.peer->isBlocked()) {
			checkTotal(1);
		}
	}, lifetime());
}

rpl::producer<QString> Blocked::title() {
	return tr::lng_settings_blocked_users();
}

base::weak_qptr<Ui::RpWidget> Blocked::createPinnedToTop(
		not_null<QWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	Ui::AddSkip(content);

	const auto blockButton = AddButtonWithIcon(
		content,
		tr::lng_blocked_list_add(),
		st::settingsButtonActive,
		{ &st::menuIconBlockSettings });
	_blockUserButton = blockButton;
	blockButton->addClickHandler([=] {
		BlockedBoxController::BlockNewPeer(controller());
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

		std::move(
			subtitleText
		) | rpl::on_next([=] {
			subtitle->entity()->resizeToWidth(content->width());
		}, subtitle->lifetime());
	}

	return base::make_weak(not_null<Ui::RpWidget*>{ content });
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

		auto controller = std::make_unique<BlockedBoxController>(
			this->controller());
		controller->setStyleOverrides(&st::settingsBlockedList);
		const auto content = listWrap->entity()->add(
			object_ptr<PeerListContent>(this, controller.get()));

		const auto state = content->lifetime().make_state<State>();
		state->controller = std::move(controller);
		state->delegate = std::make_unique<PeerListContentDelegateSimple>();

		state->delegate->setContent(content);
		state->controller->setDelegate(state->delegate.get());

		state->controller->rowsCountChanges(
		) | rpl::on_next([=](int total) {
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
				.sizeOverride = st::normalBoxLottieSize,
			},
			st::settingsBlockedListIconPadding);
		content->add(std::move(icon.widget));

		_showFinished.events(
		) | rpl::on_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, content->lifetime());

		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_blocked_list_empty_title(),
				st::changePhoneTitle),
			st::changePhoneTitlePadding,
			style::al_top);

		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_blocked_list_empty_description(),
				st::changePhoneDescription),
			st::changePhoneDescriptionPadding,
			style::al_top);

		Ui::AddSkip(content, st::settingsBlockedListIconPadding.top());
	}

	widthValue(
	) | rpl::on_next([=](int width) {
		_container->resizeToWidth(width);
	}, _container->lifetime());

	rpl::combine(
		_container->heightValue(),
		_emptinessChanges.events_starting_with(true)
	) | rpl::on_next([=](int height, bool empty) {
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

	const SectionBuildMethod buildMethod = [](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto &lifetime = container->lifetime();
		const auto highlights = lifetime.make_state<HighlightRegistry>();

		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = Window::PausedIn(
				controller,
				Window::GifPauseReason::Layer),
			.highlights = highlights,
		});

		BuildBlockedSection(builder);

		std::move(showFinished) | rpl::on_next([=] {
			for (const auto &[id, entry] : *highlights) {
				if (entry.widget) {
					controller->checkHighlightControl(
						id,
						entry.widget,
						base::duplicate(entry.args));
				}
			}
		}, lifetime);
	};

	build(_container, buildMethod);
}

void Blocked::checkTotal(int total) {
	_loading = nullptr;
	_emptinessChanges.fire(total <= 0);
}

void Blocked::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	setChildVisibleTopBottom(_container, visibleTop, visibleBottom);
}

void Blocked::showFinished() {
	Section::showFinished();
	_showFinished.fire({});
	controller()->checkHighlightControl(
		u"blocked/block-user"_q,
		_blockUserButton);
}

const auto kMeta = BuildHelper({
	.id = Blocked::Id(),
	.parentId = PrivacySecurityId(),
	.title = &tr::lng_settings_blocked_users,
	.icon = &st::menuIconBlock,
}, [](SectionBuilder &builder) {
	BuildBlockedSection(builder);
});

} // namespace

Type BlockedPeersId() {
	return Blocked::Id();
}

} // namespace Settings

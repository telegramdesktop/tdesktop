/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_global_ttl.h"

#include "api/api_self_destruct.h"
#include "apiwrap.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_changes.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "menu/menu_ttl_validator.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_calls.h"

namespace Settings {
namespace {

using namespace Builder;

class TTLRow : public ChatsListBoxController::Row {
public:
	using ChatsListBoxController::Row::Row;

	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;

};

void TTLRow::paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) {
	auto icon = history()->peer->messagesTTL()
		? &st::settingsTTLChatsOn
		: &st::settingsTTLChatsOff;
	icon->paint(
		p,
		x + st::callArrowPosition.x(),
		y + st::callArrowPosition.y(),
		outerWidth);
	auto shift = st::callArrowPosition.x()
		+ icon->width()
		+ st::callArrowSkip;
	x += shift;
	availableWidth -= shift;

	PeerListRow::paintStatusText(
		p,
		st,
		x,
		y,
		availableWidth,
		outerWidth,
		selected);
}

class TTLChatsBoxController : public ChatsListBoxController {
public:

	TTLChatsBoxController(not_null<::Main::Session*> session);

	::Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	const not_null<::Main::Session*> _session;

	rpl::lifetime _lifetime;

};

TTLChatsBoxController::TTLChatsBoxController(not_null<::Main::Session*> session)
: ChatsListBoxController(session)
, _session(session) {
}

::Main::Session &TTLChatsBoxController::session() const {
	return *_session;
}

void TTLChatsBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_settings_ttl_title());
}

void TTLChatsBoxController::rowClicked(not_null<PeerListRow*> row) {
	if (!TTLMenu::TTLValidator(nullptr, row->peer()).can()) {
		delegate()->peerListUiShow()->showToast(
			{ tr::lng_settings_ttl_select_chats_sorry(tr::now) });
		return;
	}
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<TTLChatsBoxController::Row> TTLChatsBoxController::createRow(
		not_null<History*> history) {
	const auto peer = history->peer;
	if (peer->isSelf() || peer->isRepliesChat() || peer->isVerifyCodes()) {
		return nullptr;
	} else if (peer->isChat() && peer->asChat()->amIn()) {
	} else if (peer->isMegagroup()) {
	} else if (!TTLMenu::TTLValidator(nullptr, peer).can()) {
		return nullptr;
	}
	if (session().data().contactsNoChatsList()->contains({ history })) {
		return nullptr;
	}
	auto result = std::make_unique<TTLRow>(history);
	const auto applyStatus = [=, raw = result.get()] {
		const auto ttl = peer->messagesTTL();
		raw->setCustomStatus(
			ttl
				? tr::lng_settings_ttl_select_chats_status(
					tr::now,
					lt_after_duration,
					Ui::FormatTTLAfter(ttl))
				: tr::lng_settings_ttl_select_chats_status_disabled(tr::now),
			ttl);
	};
	applyStatus();
	return result;
}

struct GlobalTTLState {
	std::shared_ptr<Ui::RadiobuttonGroup> group;
	std::shared_ptr<::Main::SessionShow> show;
	not_null<Ui::VerticalLayout*> buttons;
	QPointer<Ui::SettingsButton> customButton;
	rpl::lifetime requestLifetime;
};

void BuildTopContent(SectionBuilder &builder, rpl::producer<> showFinished) {
	builder.add([showFinished = std::move(showFinished)](
			const WidgetContext &ctx) mutable {
		const auto parent = ctx.container;
		const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(
			parent.get());
		const auto verticalLayout = parent->add(
			object_ptr<Ui::VerticalLayout>(parent.get()));

		auto icon = CreateLottieIcon(
			verticalLayout,
			{
				.name = u"ttl"_q,
				.sizeOverride = {
					st::settingsCloudPasswordIconSize,
					st::settingsCloudPasswordIconSize,
				},
			},
			st::settingsFilterIconPadding);
		std::move(
			showFinished
		) | rpl::on_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::loop);
		}, verticalLayout->lifetime());
		verticalLayout->add(std::move(icon.widget));

		verticalLayout->geometryValue(
		) | rpl::on_next([=](const QRect &r) {
			divider->setGeometry(r);
		}, divider->lifetime());

		return SectionBuilder::WidgetToAdd{};
	});
}

void RebuildButtons(
		not_null<GlobalTTLState*> state,
		not_null<Window::SessionController*> controller,
		TimeId currentTTL) {
	auto ttls = std::vector<TimeId>{
		0,
		3600 * 24,
		3600 * 24 * 7,
		3600 * 24 * 31,
	};
	if (!ranges::contains(ttls, currentTTL)) {
		ttls.push_back(currentTTL);
		ranges::sort(ttls);
	}
	if (state->buttons->count() > int(ttls.size())) {
		return;
	}

	const auto request = [=](TimeId ttl) {
		controller->session().api().selfDestruct().updateDefaultHistoryTTL(ttl);
	};

	const auto showSure = [=](TimeId ttl, bool rebuild) {
		const auto ttlText = Ui::FormatTTLAfter(ttl);
		const auto confirmed = [=] {
			if (rebuild) {
				RebuildButtons(state, controller, ttl);
			}
			state->group->setChangedCallback([=](int value) {
				state->group->setChangedCallback(nullptr);
				state->show->showToast(tr::lng_settings_ttl_after_toast(
					tr::now,
					lt_after_duration,
					{ .text = ttlText },
					tr::marked));
				state->show->hideLayer();
			});
			request(ttl);
		};
		if (state->group->value()) {
			confirmed();
			return;
		}
		state->show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_settings_ttl_after_sure(
				lt_after_duration,
				rpl::single(ttlText)),
			.confirmed = confirmed,
			.cancelled = [=](Fn<void()> &&close) {
				state->group->setChangedCallback(nullptr);
				close();
			},
			.confirmText = tr::lng_sure_enable(),
		}));
	};

	state->buttons->clear();
	for (const auto &ttl : ttls) {
		const auto ttlText = Ui::FormatTTLAfter(ttl);
		const auto button = state->buttons->add(object_ptr<Ui::SettingsButton>(
			state->buttons,
			(!ttl)
				? tr::lng_settings_ttl_after_off()
				: tr::lng_settings_ttl_after(
					lt_after_duration,
					rpl::single(ttlText)),
			st::settingsButtonNoIcon));
		button->setClickedCallback([=] {
			if (state->group->current() == ttl) {
				return;
			}
			if (!ttl) {
				state->group->setChangedCallback(nullptr);
				request(ttl);
				return;
			}
			showSure(ttl, false);
		});
		const auto radio = Ui::CreateChild<Ui::Radiobutton>(
			button,
			state->group,
			ttl,
			QString());
		radio->setAttribute(Qt::WA_TransparentForMouseEvents);
		radio->show();
		const auto padding = button->st().padding;
		button->sizeValue(
		) | rpl::on_next([=](QSize s) {
			radio->moveToLeft(
				s.width() - radio->checkRect().width() - padding.left(),
				radio->checkRect().top());
		}, radio->lifetime());
	}
	state->buttons->resizeToWidth(state->buttons->width());
}

void BuildTTLOptions(
		SectionBuilder &builder,
		not_null<GlobalTTLState*> state) {
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"auto-delete/period"_q,
		.title = tr::lng_settings_ttl_after_subtitle(),
		.keywords = { u"ttl"_q, u"auto-delete"_q, u"timer"_q },
	});

	builder.add([=](const WidgetContext &ctx) {
		const auto controller = ctx.controller;
		ctx.container->add(
			object_ptr<Ui::VerticalLayout>::fromRaw(state->buttons.get()));

		const auto &apiTTL = controller->session().api().selfDestruct();
		const auto rebuild = [=](TimeId period) {
			RebuildButtons(state, controller, period);
			state->group->setValue(period);
		};
		rebuild(apiTTL.periodDefaultHistoryTTLCurrent());
		apiTTL.periodDefaultHistoryTTL(
		) | rpl::on_next(rebuild, ctx.container->lifetime());

		return SectionBuilder::WidgetToAdd{};
	});
}

void BuildCustomButton(
		SectionBuilder &builder,
		not_null<GlobalTTLState*> state) {
	builder.add([=](const WidgetContext &ctx) {
		const auto controller = ctx.controller;
		const auto show = controller->uiShow();

		const auto showSure = [=](TimeId ttl, bool rebuild) {
			const auto ttlText = Ui::FormatTTLAfter(ttl);
			const auto confirmed = [=] {
				if (rebuild) {
					RebuildButtons(state, controller, ttl);
				}
				state->group->setChangedCallback([=](int value) {
					state->group->setChangedCallback(nullptr);
					state->show->showToast(tr::lng_settings_ttl_after_toast(
						tr::now,
						lt_after_duration,
						{ .text = ttlText },
						tr::marked));
					state->show->hideLayer();
				});
				controller->session().api().selfDestruct().updateDefaultHistoryTTL(ttl);
			};
			if (state->group->value()) {
				confirmed();
				return;
			}
			state->show->showBox(Ui::MakeConfirmBox({
				.text = tr::lng_settings_ttl_after_sure(
					lt_after_duration,
					rpl::single(ttlText)),
				.confirmed = confirmed,
				.cancelled = [=](Fn<void()> &&close) {
					state->group->setChangedCallback(nullptr);
					close();
				},
				.confirmText = tr::lng_sure_enable(),
			}));
		};

		state->customButton = ctx.container->add(object_ptr<Ui::SettingsButton>(
			ctx.container,
			tr::lng_settings_ttl_after_custom(),
			st::settingsButtonNoIcon));
		state->customButton->setClickedCallback([=] {
			show->showBox(Box(TTLMenu::TTLBox, TTLMenu::Args{
				.show = show,
				.startTtl = state->group->current(),
				.callback = [=](TimeId ttl, Fn<void()>) { showSure(ttl, true); },
				.hideDisable = true,
			}));
		});
		if (ctx.highlights) {
			ctx.highlights->push_back({
				u"auto-delete/set-custom"_q,
				{ state->customButton.data(), { .rippleShape = true } },
			});
		}

		return SectionBuilder::WidgetToAdd{};
	}, [] {
		return SearchEntry{
			.id = u"auto-delete/set-custom"_q,
			.title = tr::lng_settings_ttl_after_custom(tr::now),
			.keywords = { u"custom"_q, u"ttl"_q, u"period"_q },
		};
	});
}

void BuildApplyToExisting(
		SectionBuilder &builder,
		not_null<GlobalTTLState*> state) {
	builder.addSkip();

	builder.add([=](const WidgetContext &ctx) {
		const auto controller = ctx.controller;
		const auto session = &controller->session();

		auto footer = object_ptr<Ui::FlatLabel>(
			ctx.container,
			tr::lng_settings_ttl_after_about(
				lt_link,
				tr::lng_settings_ttl_after_about_link(
				) | rpl::map([](QString s) { return tr::link(s, 1); }),
				tr::marked),
			st::boxDividerLabel);
		footer->setLink(1, std::make_shared<LambdaClickHandler>([=] {
			auto boxController = std::make_unique<TTLChatsBoxController>(session);
			auto initBox = [=, ctrl = boxController.get()](
					not_null<PeerListBox*> box) {
				box->addButton(tr::lng_settings_apply(), [=] {
					const auto &peers = box->collectSelectedRows();
					if (peers.empty()) {
						return;
					}
					const auto &apiTTL = session->api().selfDestruct();
					const auto ttl = apiTTL.periodDefaultHistoryTTLCurrent();
					for (const auto &peer : peers) {
						peer->session().api().request(MTPmessages_SetHistoryTTL(
							peer->input(),
							MTP_int(ttl)
						)).done([=](const MTPUpdates &result) {
							peer->session().api().applyUpdates(result);
						}).send();
					}
					box->showToast(ttl
						? tr::lng_settings_ttl_select_chats_toast(
							tr::now,
							lt_count,
							peers.size(),
							lt_duration,
							{ .text = Ui::FormatTTL(ttl) },
							tr::marked)
						: tr::lng_settings_ttl_select_chats_disabled_toast(
							tr::now,
							lt_count,
							peers.size(),
							tr::marked));
					box->closeBox();
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			};
			controller->show(
				Box<PeerListBox>(std::move(boxController), std::move(initBox)));
		}));
		ctx.container->add(object_ptr<Ui::DividerLabel>(
			ctx.container,
			std::move(footer),
			st::defaultBoxDividerLabelPadding));

		return SectionBuilder::WidgetToAdd{};
	});
}

class GlobalTTL : public Section<GlobalTTL> {
public:
	GlobalTTL(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;
	void showFinished() override;

private:
	void setupContent();

	std::shared_ptr<GlobalTTLState> _state;
	rpl::event_stream<> _showFinished;

};

GlobalTTL::GlobalTTL(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _state(std::make_shared<GlobalTTLState>(GlobalTTLState{
	.group = std::make_shared<Ui::RadiobuttonGroup>(0),
	.show = controller->uiShow(),
	.buttons = Ui::CreateChild<Ui::VerticalLayout>(this),
})) {
	setupContent();
}

rpl::producer<QString> GlobalTTL::title() {
	return tr::lng_settings_ttl_title();
}

void GlobalTTL::setupContent() {
	setFocusPolicy(Qt::StrongFocus);
	setFocus();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto state = _state;

	const SectionBuildMethod buildMethod = [state](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto &lifetime = container->lifetime();
		const auto highlights = lifetime.make_state<HighlightRegistry>();
		const auto isPaused = Window::PausedIn(
			controller,
			Window::GifPauseReason::Layer);
		auto showFinishedDup = rpl::duplicate(showFinished);
		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = isPaused,
			.highlights = highlights,
		});

		BuildTopContent(builder, std::move(showFinishedDup));
		BuildTTLOptions(builder, state.get());
		BuildCustomButton(builder, state.get());
		BuildApplyToExisting(builder, state.get());

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

	build(content, buildMethod);

	Ui::ResizeFitChild(this, content);
}

void GlobalTTL::showFinished() {
	_showFinished.fire({});
	Section<GlobalTTL>::showFinished();
}

const auto kMeta = BuildHelper({
	.id = GlobalTTL::Id(),
	.parentId = PrivacySecurityId(),
	.title = &tr::lng_settings_ttl_title,
	.icon = &st::menuIconTTL,
}, [](SectionBuilder &builder) {
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"auto-delete/period"_q,
			.title = tr::lng_settings_ttl_after_subtitle(tr::now),
			.keywords = { u"ttl"_q, u"auto-delete"_q, u"timer"_q },
		};
	});

	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"auto-delete/set-custom"_q,
			.title = tr::lng_settings_ttl_after_custom(tr::now),
			.keywords = { u"custom"_q, u"ttl"_q, u"period"_q },
		};
	});
});

} // namespace

Type GlobalTTLId() {
	return GlobalTTL::Id();
}

namespace Builder {

SectionBuildMethod GlobalTTLSection = kMeta.build;

} // namespace Builder
} // namespace Settings

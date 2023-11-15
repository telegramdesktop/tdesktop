/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_global_ttl.h"

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
#include "styles/style_settings.h"
#include "styles/style_calls.h"

namespace Settings {
namespace {

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

	TTLChatsBoxController(not_null<Main::Session*> session);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

private:
	const not_null<Main::Session*> _session;

	rpl::lifetime _lifetime;

};

TTLChatsBoxController::TTLChatsBoxController(not_null<Main::Session*> session)
: ChatsListBoxController(session)
, _session(session) {
}

Main::Session &TTLChatsBoxController::session() const {
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
	if (history->peer->isSelf() || history->peer->isRepliesChat()) {
		return nullptr;
	} else if (history->peer->isChat() && history->peer->asChat()->amIn()) {
	} else if (history->peer->isMegagroup()) {
	} else if (!TTLMenu::TTLValidator(nullptr, history->peer).can()) {
		return nullptr;
	}
	if (session().data().contactsNoChatsList()->contains({ history })) {
		return nullptr;
	}
	auto result = std::make_unique<TTLRow>(history);
	const auto applyStatus = [=, raw = result.get()] {
		const auto ttl = history->peer->messagesTTL();
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

void SetupTopContent(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<> showFinished) {
	const auto divider = Ui::CreateChild<Ui::BoxContentDivider>(parent.get());
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
	) | rpl::start_with_next([animate = std::move(icon.animate)] {
		animate(anim::repeat::loop);
	}, verticalLayout->lifetime());
	verticalLayout->add(std::move(icon.widget));

	verticalLayout->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		divider->setGeometry(r);
	}, divider->lifetime());

}

} // namespace

class GlobalTTL : public Section<GlobalTTL> {
public:
	GlobalTTL(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

	void showFinished() override final;

private:
	void rebuildButtons(TimeId currentTTL) const;
	void showSure(TimeId ttl, bool rebuild) const;

	void request(TimeId ttl) const;

	const not_null<Window::SessionController*> _controller;
	const std::shared_ptr<Ui::RadiobuttonGroup> _group;
	const std::shared_ptr<Main::SessionShow> _show;

	not_null<Ui::VerticalLayout*> _buttons;

	rpl::event_stream<> _showFinished;
	rpl::lifetime _requestLifetime;

};

GlobalTTL::GlobalTTL(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent)
, _controller(controller)
, _group(std::make_shared<Ui::RadiobuttonGroup>(0))
, _show(controller->uiShow())
, _buttons(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	setupContent();
}

rpl::producer<QString> GlobalTTL::title() {
	return tr::lng_settings_ttl_title();
}

void GlobalTTL::request(TimeId ttl) const {
	_controller->session().api().selfDestruct().updateDefaultHistoryTTL(ttl);
}

void GlobalTTL::showSure(TimeId ttl, bool rebuild) const {
	const auto ttlText = Ui::FormatTTLAfter(ttl);
	const auto confirmed = [=] {
		if (rebuild) {
			rebuildButtons(ttl);
		}
		_group->setChangedCallback([=](int value) {
			_group->setChangedCallback(nullptr);
			_show->showToast(tr::lng_settings_ttl_after_toast(
				tr::now,
				lt_after_duration,
				{ .text = ttlText },
				Ui::Text::WithEntities));
			_show->hideLayer(); // Don't use close().
		});
		request(ttl);
	};
	if (_group->value()) {
		confirmed();
		return;
	}
	_show->showBox(Ui::MakeConfirmBox({
		.text = tr::lng_settings_ttl_after_sure(
			lt_after_duration,
			rpl::single(ttlText)),
		.confirmed = confirmed,
		.cancelled = [=](Fn<void()> &&close) {
			_group->setChangedCallback(nullptr);
			close();
		},
		.confirmText = tr::lng_sure_enable(),
	}));
}

void GlobalTTL::rebuildButtons(TimeId currentTTL) const {
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
	if (_buttons->count() > ttls.size()) {
		return;
	}
	_buttons->clear();
	for (const auto &ttl : ttls) {
		const auto ttlText = Ui::FormatTTLAfter(ttl);
		const auto button = _buttons->add(object_ptr<Ui::SettingsButton>(
			_buttons,
			(!ttl)
				? tr::lng_settings_ttl_after_off()
				: tr::lng_settings_ttl_after(
					lt_after_duration,
					rpl::single(ttlText)),
			st::settingsButtonNoIcon));
		button->setClickedCallback([=] {
			if (_group->value() == ttl) {
				return;
			}
			if (!ttl) {
				_group->setChangedCallback(nullptr);
				request(ttl);
				return;
			}
			showSure(ttl, false);
		});
		const auto radio = Ui::CreateChild<Ui::Radiobutton>(
			button,
			_group,
			ttl,
			QString());
		radio->setAttribute(Qt::WA_TransparentForMouseEvents);
		radio->show();
		button->sizeValue(
		) | rpl::start_with_next([=] {
			radio->moveToRight(0, radio->checkRect().top());
		}, radio->lifetime());
	}
	_buttons->resizeToWidth(width());
}

void GlobalTTL::setupContent() {
	setFocusPolicy(Qt::StrongFocus);
	setFocus();

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	SetupTopContent(content, _showFinished.events());

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_ttl_after_subtitle());

	content->add(object_ptr<Ui::VerticalLayout>::fromRaw(_buttons));

	{
		const auto &apiTTL = _controller->session().api().selfDestruct();
		const auto rebuild = [=](TimeId period) {
			rebuildButtons(period);
			_group->setValue(period);
		};
		rebuild(apiTTL.periodDefaultHistoryTTLCurrent());
		apiTTL.periodDefaultHistoryTTL(
		) | rpl::start_with_next(rebuild, content->lifetime());
	}

	const auto show = _controller->uiShow();
	content->add(object_ptr<Ui::SettingsButton>(
		content,
		tr::lng_settings_ttl_after_custom(),
		st::settingsButtonNoIcon))->setClickedCallback([=] {
		struct Args {
			std::shared_ptr<Ui::Show> show;
			TimeId startTtl;
			rpl::producer<QString> about;
			Fn<void(TimeId)> callback;
		};

		show->showBox(Box(TTLMenu::TTLBox, TTLMenu::Args{
			.show = show,
			.startTtl = _group->value(),
			.callback = [=](TimeId ttl, Fn<void()>) { showSure(ttl, true); },
			.hideDisable = true,
		}));
	});

	Ui::AddSkip(content);

	auto footer = object_ptr<Ui::FlatLabel>(
		content,
		tr::lng_settings_ttl_after_about(
			lt_link,
			tr::lng_settings_ttl_after_about_link(
			) | rpl::map([](QString s) { return Ui::Text::Link(s, 1); }),
			Ui::Text::WithEntities),
		st::boxDividerLabel);
	footer->setLink(1, std::make_shared<LambdaClickHandler>([=] {
		const auto session = &_controller->session();
		auto controller = std::make_unique<TTLChatsBoxController>(session);
		auto initBox = [=, controller = controller.get()](
				not_null<PeerListBox*> box) {
			box->addButton(tr::lng_settings_apply(), crl::guard(this, [=] {
				const auto &peers = box->collectSelectedRows();
				if (peers.empty()) {
					return;
				}
				const auto &apiTTL = session->api().selfDestruct();
				const auto ttl = apiTTL.periodDefaultHistoryTTLCurrent();
				for (const auto &peer : peers) {
					peer->session().api().request(MTPmessages_SetHistoryTTL(
						peer->input,
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
						Ui::Text::WithEntities)
					: tr::lng_settings_ttl_select_chats_disabled_toast(
						tr::now,
						lt_count,
						peers.size(),
						Ui::Text::WithEntities));
				box->closeBox();
			}));
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		};
		_controller->show(
			Box<PeerListBox>(std::move(controller), std::move(initBox)));
	}));
	content->add(object_ptr<Ui::DividerLabel>(
		content,
		std::move(footer),
		st::defaultBoxDividerLabelPadding));

	Ui::ResizeFitChild(this, content);
}

void GlobalTTL::showFinished() {
	_showFinished.fire({});
}

Type GlobalTTLId() {
	return GlobalTTL::Id();
}

} // namespace Settings

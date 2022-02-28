/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_rtmp.h"

#include "apiwrap.h"
#include "base/event_filter.h"
#include "calls/group/calls_group_common.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "settings/settings_common.h" // AddDivider.
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_calls.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QGuiApplication>
#include <QStyle>

namespace Calls::Group {
namespace {

constexpr auto kPasswordCharAmount = 24;

void StartWithBox(
		not_null<Ui::GenericBox*> box,
		Fn<void()> done,
		Fn<void()> revoke,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox,
		Fn<void(QString)> showToast,
		rpl::producer<StartRtmpProcess::Data> &&data) {
	struct State {
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = box->lifetime().make_state<State>();

	StartRtmpProcess::FillRtmpRows(
		box->verticalLayout(),
		true,
		false,
		std::move(showBox),
		std::move(showToast),
		std::move(data),
		&st::boxLabel,
		&st::groupCallRtmpShowButton,
		&st::settingsSubsectionTitle,
		&st::attentionBoxButton);

	box->setTitle(tr::lng_group_call_rtmp_title());

	Settings::AddDividerText(
		box->verticalLayout(),
		tr::lng_group_call_rtmp_info());

	box->addButton(tr::lng_group_call_rtmp_start(), done);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->setWidth(st::infoDesiredWidth);
	{
		const auto top = box->addTopButton(st::infoTopBarMenu);
		top->setClickedCallback([=] {
			state->menu = base::make_unique_q<Ui::PopupMenu>(
				top,
				st::popupMenuWithIcons);
			state->menu->addAction(
				tr::lng_group_invite_context_revoke(tr::now),
				revoke,
				&st::menuIconRemove);
			state->menu->moveToRight(
				st::groupCallRtmpTopBarMenuPosition.x(),
				st::groupCallRtmpTopBarMenuPosition.y());
			state->menu->setForcedOrigin(
				Ui::PanelAnimation::Origin::TopRight);
			state->menu->popup(QCursor::pos());
			return true;
		});
	}

}

} // namespace

StartRtmpProcess::~StartRtmpProcess() {
	if (_request) {
		_request->peer->session().api().request(_request->id).cancel();
	}
}

void StartRtmpProcess::start(
		not_null<PeerData*> peer,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox,
		Fn<void(QString)> showToast,
		Fn<void(JoinInfo)> done) {
	Expects(done != nullptr);

	const auto session = &peer->session();
	if (_request) {
		if (_request->peer == peer) {
			_request->showBox = std::move(showBox);
			_request->showToast = std::move(showToast);
			_request->done = std::move(done);
			return;
		}
		session->api().request(_request->id).cancel();
		_request = nullptr;
	}
	_request = std::make_unique<RtmpRequest>(
		RtmpRequest{
			.peer = peer,
			.showBox = std::move(showBox),
			.showToast = std::move(showToast),
			.done = std::move(done) });
	session->account().sessionChanges(
	) | rpl::start_with_next([=] {
		_request = nullptr;
	}, _request->lifetime);

	requestUrl(false);
}

void StartRtmpProcess::requestUrl(bool revoke) {
	const auto session = &_request->peer->session();
	_request->id = session->api().request(MTPphone_GetGroupCallStreamRtmpUrl(
		_request->peer->input,
		MTP_bool(revoke)
	)).done([=](const MTPphone_GroupCallStreamRtmpUrl &result) {
		auto data = result.match([&](
				const MTPDphone_groupCallStreamRtmpUrl &data) {
			return Data{ .url = qs(data.vurl()), .key = qs(data.vkey()) };
		});
		processUrl(std::move(data));
	}).fail([=] {
		_request->showToast(Lang::Hard::ServerError());
	}).send();
}

void StartRtmpProcess::processUrl(Data data) {
	if (!_request->box) {
		createBox();
	}
	_request->data = std::move(data);
}

void StartRtmpProcess::finish(JoinInfo info) {
	const auto done = std::move(_request->done);
	const auto box = _request->box;
	const auto current = _request->data.current();
	_request = nullptr;
	info.rtmpUrl = current.url;
	info.rtmpKey = current.key;
	done(std::move(info));
	if (const auto strong = box.data()) {
		strong->closeBox();
	}
}

void StartRtmpProcess::createBox() {
	auto done = [=] {
		const auto peer = _request->peer;
		finish({ .peer = peer, .joinAs = peer, .rtmp = true });
	};
	auto revoke = [=] {
		const auto guard = base::make_weak(&_request->guard);
		_request->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_group_call_rtmp_revoke_sure(),
			.confirmed = crl::guard(guard, [=](Fn<void()> &&close) {
				requestUrl(true);
				close();
			}),
			.confirmText = tr::lng_group_invite_context_revoke(),
		}));
	};
	auto object = Box(
		StartWithBox,
		std::move(done),
		std::move(revoke),
		_request->showBox,
		_request->showToast,
		_request->data.value());
	object->boxClosing(
	) | rpl::start_with_next([=] {
		_request = nullptr;
	}, _request->lifetime);
	_request->box = Ui::MakeWeak(object.data());
	_request->showBox(std::move(object));
}

void StartRtmpProcess::FillRtmpRows(
		not_null<Ui::VerticalLayout*> container,
		bool divider,
		bool disabledMenuForLabels,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox,
		Fn<void(QString)> showToast,
		rpl::producer<StartRtmpProcess::Data> &&data,
		const style::FlatLabel *labelStyle,
		const style::IconButton *showButtonStyle,
		const style::FlatLabel *subsectionTitleStyle,
		const style::RoundButton *attentionButtonStyle) {
	struct State {
		rpl::variable<bool> hidden = true;
		rpl::variable<QString> key;
		rpl::variable<QString> url;
		bool warned = false;
	};

	const auto &rowPadding = st::boxRowPadding;

	const auto passChar = QChar(container->style()->styleHint(
		QStyle::SH_LineEdit_PasswordCharacter));
	const auto state = container->lifetime().make_state<State>();
	state->key = rpl::duplicate(
		data
	) | rpl::map([=](const auto &d) { return d.key; });
	state->url = std::move(
		data
	) | rpl::map([=](const auto &d) { return d.url; });

	const auto addButton = [&](
			bool key,
			rpl::producer<QString> &&text) {
		const auto &padding = st::groupCallRtmpCopyButton.padding;

		auto wrap = object_ptr<Ui::RpWidget>(container);
		auto button = Ui::CreateChild<Ui::RoundButton>(
			wrap.data(),
			rpl::duplicate(text),
			st::groupCallRtmpCopyButton);
		button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
		button->setClickedCallback(key
			? Fn<void()>([=] {
				QGuiApplication::clipboard()->setText(state->key.current());
				showToast(tr::lng_group_call_rtmp_key_copied(tr::now));
			})
			: Fn<void()>([=] {
				QGuiApplication::clipboard()->setText(state->url.current());
				showToast(tr::lng_group_call_rtmp_url_copied(tr::now));
			}));
		const auto weak = container->add(std::move(wrap), rowPadding);
		button->heightValue(
		) | rpl::start_with_next([=](int height) {
			weak->resize(weak->width(), height);
		}, container->lifetime());
		return weak;
	};

	const auto addLabel = [&](
			rpl::producer<QString> &&text,
			const style::FlatLabel &st) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(container, std::move(text), st),
			st::boxRowPadding + QMargins(0, 0, showButtonStyle->width, 0));
		label->setSelectable(true);
		label->setBreakEverywhere(true);
		if (disabledMenuForLabels) {
			base::install_event_filter(label, [=](not_null<QEvent*> e) {
				return (e->type() == QEvent::ContextMenu)
					? base::EventFilterResult::Cancel
					: base::EventFilterResult::Continue;
			});
		}
		return label;
	};

	// Server URL.
	{
		// Settings::AddSubsectionTitle
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_group_call_rtmp_url_subtitle(),
				*subsectionTitleStyle),
			st::settingsSubsectionTitlePadding
				+ st::groupCallRtmpSubsectionTitleAddPadding);
	}

	auto urlLabelContent = state->url.value();
	addLabel(std::move(urlLabelContent), *labelStyle);
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::groupCallRtmpUrlSkip));
	addButton(false, tr::lng_group_call_rtmp_url_copy());
	//

	if (divider) {
		Settings::AddDivider(container);
	}

	// Stream Key.
	container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::groupCallRtmpKeySubsectionTitleSkip));

	{
		// Settings::AddSubsectionTitle
		container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_group_call_rtmp_key_subtitle(),
				*subsectionTitleStyle),
			st::settingsSubsectionTitlePadding
				+ st::groupCallRtmpSubsectionTitleAddPadding);
	}

	auto keyLabelContent = rpl::combine(
		state->hidden.value(),
		state->key.value()
	) | rpl::map([passChar](bool hidden, const QString &key) {
		return key.isEmpty()
			? QString()
			: hidden
			? QString().fill(passChar, kPasswordCharAmount)
			: key;
	}) | rpl::after_next([=] {
		container->resizeToWidth(container->widthNoMargins());
	});
	const auto streamKeyLabel = addLabel(
		std::move(keyLabelContent),
		*labelStyle);
	streamKeyLabel->setSelectable(false);
	const auto streamKeyButton = Ui::CreateChild<Ui::IconButton>(
		container.get(),
		*showButtonStyle);

	streamKeyLabel->topValue(
	) | rpl::start_with_next([=, right = rowPadding.right()](int top) {
		streamKeyButton->moveToRight(
			st::groupCallRtmpShowButtonPosition.x(),
			top + st::groupCallRtmpShowButtonPosition.y());
		streamKeyButton->raise();
	}, container->lifetime());
	streamKeyButton->addClickHandler([=] {
		const auto toggle = [=] {
			const auto newValue = !state->hidden.current();
			state->hidden = newValue;
			streamKeyLabel->setSelectable(!newValue);
			streamKeyLabel->setAttribute(
				Qt::WA_TransparentForMouseEvents,
				newValue);
		};
		if (!state->warned && state->hidden.current()) {
			showBox(Ui::MakeConfirmBox({
				.text = tr::lng_group_call_rtmp_key_warning(
					Ui::Text::RichLangValue),
				.confirmed = [=](Fn<void()> &&close) {
					state->warned = true;
					toggle();
					close();
				},
				.confirmText = tr::lng_from_request_understand(),
				.cancelText = tr::lng_close(),
				.confirmStyle = attentionButtonStyle,
				.labelStyle = labelStyle,
			}));
		} else {
			toggle();
		}
	});

	addButton(true, tr::lng_group_call_rtmp_key_copy());
	//
}

} // namespace Calls::Group

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_box.h"

#include "base/unixtime.h"
#include "data/data_cloud_file.h"
#include "data/data_file_origin.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/button_busy.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect.h"

#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

using Phase = TonConnectBoxPhase;

struct AppIcon {
	WebFileLocation location;
	QImage image;
	Data::CloudFile file;
};

struct State {
	std::optional<TonConnectBoxState> built;
	Fn<void(const TonConnectHeaderState &, anim::type)> header;
	QPointer<Ui::VerticalLayout> body;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> proof;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> error;
	QPointer<Ui::FlatLabel> notice;
	QPointer<Ui::RoundButton> connect;
	QPointer<Ui::RoundButton> restore;
	std::shared_ptr<bool> markPlayed = std::make_shared<bool>();
	bool busy = false;
};

struct AppRow {
	TonConnectSessionId id = 0;
	QString name;
	QString domain;
	WebFileLocation icon;
	TimeId date = 0;

	friend bool operator==(const AppRow &, const AppRow &) = default;
};

struct AppsState {
	std::vector<AppRow> shown;
	base::flat_map<WebFileLocation, std::unique_ptr<AppIcon>> icons;
	base::flat_map<TonConnectSessionId, QPointer<Ui::RoundButton>> buttons;
	bool built = false;
	bool loaded = false;
};

[[nodiscard]] bool SameBody(
		const TonConnectBoxState &built,
		const TonConnectBoxState &now) {
	const auto kind = [](Phase phase) {
		return (phase == Phase::Notice || phase == Phase::Restore)
			? phase
			: Phase::Confirm;
	};
	return (kind(built.phase) == kind(now.phase));
}

[[nodiscard]] QImage PrepareIcon(QImage image, int size) {
	if (image.isNull()
		|| image.width() * 20 < image.height()
		|| image.height() * 20 < image.width()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	const auto full = size * ratio;
	image = image.scaled(
		full,
		full,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	if (image.width() > full || image.height() > full) {
		image = image.copy(
			(image.width() - full) / 2,
			(image.height() - full) / 2,
			full,
			full);
	}
	if (image.isNull()) {
		return QImage();
	}
	image = Images::Circle(std::move(image));
	image.setDevicePixelRatio(ratio);
	return image;
}

void LoadIcon(
		not_null<AppIcon*> icon,
		not_null<Main::Session*> session,
		int size,
		Fn<void()> repaint) {
	icon->file.clear();
	icon->image = QImage();
	if (icon->location.isNull()) {
		repaint();
		return;
	}
	icon->file.location = ImageLocation(
		DownloadLocation{ icon->location },
		0,
		0);
	Data::LoadCloudFile(
		session,
		icon->file,
		Data::FileOrigin(),
		LoadFromCloudOrLocal,
		false,
		0,
		nullptr,
		[=](QImage image, QByteArray) {
			icon->image = PrepareIcon(std::move(image), size);
			repaint();
		},
		[=](bool) {
			repaint();
		});
}

void PaintIcon(QPainter &p, QRect disc, const QImage &image) {
	auto hq = PainterHighQualityEnabler(p);
	if (image.isNull()) {
		p.setBrush(st::windowBgOver);
		p.setPen(Qt::NoPen);
		p.drawEllipse(disc);
		st::walletConnectIconPlaceholder.paintInCenter(p, disc);
	} else {
		p.drawImage(disc, image);
	}
}

void FillBody(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const std::shared_ptr<Main::SessionShow> &show,
		const TonConnectBoxState &now,
		Fn<void()> connect,
		Fn<void()> restore) {
	const auto content = state->body.data();
	content->clear();
	if (now.phase == Phase::Notice) {
		state->notice = content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				now.notice,
				st::walletConnectTextLabel),
			st::walletConnectTextMargin,
			style::al_top);
		const auto close = SetTonConnectButtons(
			box,
			nullptr,
			tr::lng_close()).primary;
		close->setClickedCallback([=] { box->closeBox(); });
		return;
	} else if (now.phase == Phase::Restore) {
		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_wallet_connect_restore(),
				st::walletConnectTextLabel),
			st::walletConnectTextMargin,
			style::al_top
		)->setTryMakeSimilarLines(true);
		const auto buttons = SetTonConnectButtons(
			box,
			tr::lng_cancel(),
			tr::lng_wallet_restore_title());
		buttons.secondary->setClickedCallback([=] { box->closeBox(); });
		buttons.primary->setClickedCallback([=] {
			if (!state->busy) {
				restore();
			}
		});
		state->restore = buttons.primary;
		return;
	}
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_wallet_connect_permission(),
			st::walletConnectTextLabel),
		st::walletConnectTextMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	state->proof = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_wallet_connect_proof(),
				st::walletConnectTextLabel),
			st::walletConnectTextMargin),
		style::margins(),
		style::al_top);
	state->proof->entity()->setTryMakeSimilarLines(true);
	content->add(
		MakeWalletCard(content, show, state->markPlayed),
		st::walletConnectCardMargin,
		style::al_top);
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			tr::lng_wallet_connect_reassurance(),
			st::walletConnectCaptionLabel),
		st::walletConnectCaptionMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
	state->error = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(content, st::walletConnectErrorLabel),
			style::margins(0, st::defaultVerticalListSkip, 0, 0)),
		st::boxRowPadding,
		style::al_top);
	const auto buttons = SetTonConnectButtons(
		box,
		tr::lng_cancel(),
		tr::lng_wallet_connect_button());
	buttons.secondary->setClickedCallback([=] { box->closeBox(); });
	buttons.primary->setClickedCallback([=] {
		if (!state->busy) {
			connect();
		}
	});
	state->connect = buttons.primary;
}

void UpdateState(
		not_null<State*> state,
		const TonConnectBoxState &now,
		anim::type animated) {
	const auto loading = (now.phase == Phase::Loading);
	state->header({
		.title = (loading
			? tr::lng_contacts_loading(tr::now)
			: now.name.isEmpty()
			? tr::lng_wallet_connect_title(tr::now)
			: tr::lng_wallet_connect_title_app(tr::now, lt_name, now.name)),
		.domain = now.domain,
		.icon = now.icon,
		.loading = loading,
	}, animated);
	if (const auto proof = state->proof.data()) {
		proof->toggle(now.proof, animated);
	}
	if (const auto error = state->error.data()) {
		if (!now.error.isEmpty()) {
			error->entity()->setText(now.error);
		}
		error->toggle(!now.error.isEmpty(), animated);
	}
	if (const auto notice = state->notice.data()) {
		notice->setText(now.notice);
	}
	state->busy = (now.phase == Phase::Restore)
		? now.busy
		: (now.phase != Phase::Confirm);
	Ui::SetButtonBusy(state->connect.data(), state->busy);
	Ui::SetButtonBusy(state->restore.data(), now.busy);
}

[[nodiscard]] std::vector<AppRow> CollectApps(const TonConnect &store) {
	auto result = std::vector<AppRow>();
	for (const auto &[id, info] : store.sessions()) {
		if (!TonConnectSessionConnected(info)) {
			continue;
		}
		const auto &manifest = info.manifest;
		auto name = manifest ? TonConnectManifestName(*manifest) : QString();
		if (name.isEmpty()) {
			name = tr::lng_wallet_apps_unknown(tr::now);
		}
		result.push_back({
			.id = id,
			.name = std::move(name),
			.domain = manifest ? TonConnectHost(manifest->url) : QString(),
			.icon = manifest ? manifest->icon : WebFileLocation(),
			.date = info.date,
		});
	}
	ranges::sort(result, [](const AppRow &a, const AppRow &b) {
		return (a.date != b.date) ? (a.date > b.date) : (a.id > b.id);
	});
	return result;
}

[[nodiscard]] not_null<AppIcon*> ResolveIcon(
		not_null<AppsState*> state,
		not_null<Main::Session*> session,
		not_null<Ui::VerticalLayout*> list,
		const WebFileLocation &location) {
	auto &icon = state->icons[location];
	if (!icon) {
		icon = std::make_unique<AppIcon>();
		icon->location = location;
		LoadIcon(icon.get(), session, st::walletRowIconSize, [=] {
			list->update();
		});
	}
	return icon.get();
}

[[nodiscard]] not_null<Ui::RoundButton*> AddAppRow(
		not_null<Ui::VerticalLayout*> list,
		not_null<AppsState*> state,
		not_null<Main::Session*> session,
		const AppRow &row,
		Fn<void()> disconnect) {
	const auto wrap = list->add(
		object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
			list,
			object_ptr<Ui::VerticalLayout>(list),
			st::walletRowPadding));
	const auto inner = wrap->entity();
	inner->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		wrap,
		tr::lng_settings_disconnect(),
		st::attentionBoxButton);
	button->setClickedCallback(std::move(disconnect));
	const auto reserve = style::margins(
		0,
		0,
		button->width() + st::walletRowSkip,
		0);
	inner->add(
		object_ptr<Ui::FlatLabel>(inner, row.name, st::walletAppsNameLabel),
		reserve);
	if (!row.domain.isEmpty()) {
		Ui::AddSkip(inner, st::walletRowSkip);
		const auto domain = inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				row.domain,
				st::walletAppsDomainLabel),
			reserve);
		domain->setBreakEverywhere(true);
	}
	if (row.date) {
		Ui::AddSkip(inner, st::walletRowSkip);
		inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				langDateTime(base::unixtime::parse(row.date)),
				st::walletAppsDateLabel),
			reserve);
	}

	const auto icon = row.icon.isNull()
		? nullptr
		: ResolveIcon(state, session, list, row.icon).get();
	const auto circle = Ui::CreateChild<Ui::RpWidget>(wrap);
	circle->resize(st::walletRowIconSize, st::walletRowIconSize);
	circle->setAttribute(Qt::WA_TransparentForMouseEvents);
	circle->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(circle);
		PaintIcon(p, circle->rect(), icon ? icon->image : QImage());
	}, circle->lifetime());
	Ui::ToggleChildrenVisibility(wrap, true);
	wrap->geometryValue(
	) | rpl::on_next([=](const QRect &g) {
		circle->moveToLeft(
			st::walletRowIconLeft,
			(g.height() - circle->height()) / 2,
			g.width());
		button->moveToRight(
			st::walletRowPadding.right(),
			(g.height() - button->height()) / 2,
			g.width());
	}, wrap->lifetime());
	return button;
}

void AddAppsPlaceholder(not_null<Ui::VerticalLayout*> list, bool loading) {
	const auto band = list->add(
		object_ptr<Ui::FixedHeightWidget>(list, st::noContactsHeight));
	if (loading) {
		const auto &radial = st::walletBusyBoxLoading;
		const auto side = radial.size.height() + 2 * radial.thickness;
		const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
			band,
			side,
			&radial);
		Info::Statistics::AddChildToWidgetCenter(band, indicator);
		indicator->show();
		return;
	}
	band->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(band);
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(
			band->rect(),
			tr::lng_wallet_apps_empty(tr::now),
			style::al_center);
	}, band->lifetime());
}

void RefreshApps(
		not_null<Ui::VerticalLayout*> list,
		not_null<AppsState*> state,
		const std::shared_ptr<Main::SessionShow> &show) {
	auto &store = show->session().wallet().tonConnect();
	auto apps = CollectApps(store);
	const auto loaded = store.loaded();
	if (!state->built
		|| apps != state->shown
		|| (apps.empty() && loaded != state->loaded)) {
		state->built = true;
		state->shown = std::move(apps);
		state->loaded = loaded;
		list->clear();
		state->buttons.clear();
		if (state->shown.empty()) {
			AddAppsPlaceholder(list, !loaded);
		}
		for (const auto &row : state->shown) {
			const auto button = AddAppRow(
				list,
				state,
				&show->session(),
				row,
				[=, id = row.id, name = row.name] {
					show->showBox(Ui::MakeConfirmBox({
						.text = tr::lng_wallet_apps_disconnect_sure(
							tr::now,
							lt_name,
							name),
						.confirmed = [=](Fn<void()> close) {
							close();
							show->session().wallet().tonConnect().disconnect(
								show,
								id);
						},
						.confirmText = tr::lng_settings_disconnect(),
						.confirmStyle = &st::attentionBoxButton,
						.title = tr::lng_wallet_apps_disconnect_title(),
					}));
				});
			state->buttons.emplace(row.id, button.get());
		}
		if (const auto width = list->width()) {
			list->resizeToWidth(width);
		}
	}
	for (const auto &[id, button] : state->buttons) {
		Ui::SetButtonBusy(button.data(), store.disconnecting(id));
	}
}

} // namespace

auto AddTonConnectHeader(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session)
-> Fn<void(const TonConnectHeaderState &, anim::type)> {
	const auto row = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			st::walletConnectIconSize),
		st::walletConnectIconMargin);
	const auto icon = row->lifetime().make_state<AppIcon>();
	row->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		const auto size = row->height();
		const auto disc = QRect((row->width() - size) / 2, 0, size, size);
		if (!disc.intersects(clip)) {
			return;
		}
		auto p = QPainter(row);
		PaintIcon(p, disc, icon->image);
	}, row->lifetime());

	const auto &loading = st::walletBusyBoxLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto spinner = Info::Statistics::InfiniteRadialAnimationWidget(
		row,
		side,
		&loading);
	Info::Statistics::AddChildToWidgetCenter(row, spinner);

	const auto title = container->add(
		object_ptr<Ui::FlatLabel>(container, st::walletConnectTitleLabel),
		st::walletConnectTitleMargin,
		style::al_top);
	const auto domain = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				st::walletConnectDomainLabel),
			st::walletConnectDomainMargin),
		style::margins(),
		style::al_top);
	return [=](const TonConnectHeaderState &now, anim::type animated) {
		title->setText(now.title);
		if (!now.domain.isEmpty()) {
			domain->entity()->setText(now.domain);
		}
		domain->toggle(!now.domain.isEmpty(), animated);
		spinner->setVisible(now.loading);
		if (icon->location != now.icon) {
			icon->location = now.icon;
			LoadIcon(icon, session, st::walletConnectIconSize, [=] {
				row->update();
			});
		}
	};
}

TonConnectButtons SetTonConnectButtons(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> secondary,
		rpl::producer<QString> primary) {
	Expects(secondary || primary);

	box->clearButtons();
	// clearButtons() drops the close button too.
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto pair = (secondary && primary);
	const auto edge = box->getDelegate()->style().buttonPadding.right();
	const auto row = box->width() - 2 * edge;
	const auto width = pair ? ((row - st::walletButtonsSkip) / 2) : row;
	const auto add = [&](
			rpl::producer<QString> text,
			const style::RoundButton &st,
			bool left) -> Ui::RoundButton* {
		if (!text) {
			return nullptr;
		}
		const auto button = left
			? box->addLeftButton(std::move(text), nullptr, st)
			: box->addButton(std::move(text), st);
		button->setFullWidth(width);
		return button.data();
	};
	return {
		.secondary = add(
			std::move(secondary),
			st::walletConnectCancelButton,
			pair),
		.primary = add(std::move(primary), st::walletSendButton, false),
	};
}

void TonConnectBox(not_null<Ui::GenericBox*> box, TonConnectBoxArgs args) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletConnectBox);
	box->setNoContentMargin(true);

	const auto state = box->lifetime().make_state<State>();
	const auto reported = std::make_shared<bool>(false);
	const auto dismiss = [=, dismissed = args.dismissed] {
		if (!std::exchange(*reported, true) && dismissed) {
			dismissed();
		}
	};
	box->boxClosing() | rpl::on_next(dismiss, box->lifetime());
	box->lifetime().add(dismiss);

	const auto show = args.show;
	state->header = AddTonConnectHeader(
		box->verticalLayout(),
		&show->session());
	state->body = box->verticalLayout()->add(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);

	const auto connect = args.connect;
	const auto restore = args.restore;
	std::move(
		args.state
	) | rpl::on_next([=](const TonConnectBoxState &now) {
		const auto rebuild = !state->built || !SameBody(*state->built, now);
		if (rebuild) {
			FillBody(box, state, show, now, connect, restore);
		}
		UpdateState(
			state,
			now,
			rebuild ? anim::type::instant : anim::type::normal);
		state->built = now;
	}, box->lifetime());
}

void TonConnectAppsBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	auto &wallet = show->session().wallet();
	if (wallet.presence() != Presence::Ready) {
		box->closeBox();
		return;
	}
	box->setTitle(tr::lng_wallet_apps_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	const auto list = container->add(
		object_ptr<Ui::VerticalLayout>(container));
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_wallet_apps_about());

	const auto state = box->lifetime().make_state<AppsState>();
	auto &store = wallet.tonConnect();
	store.updates(
	) | rpl::on_next([=](TonConnectSessionId) {
		RefreshApps(list, state, show);
	}, box->lifetime());
	store.ensureLoaded();
	RefreshApps(list, state, show);

	wallet.presenceValue(
	) | rpl::filter([](Presence presence) {
		return (presence != Presence::Ready);
	}) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());

	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace Wallet

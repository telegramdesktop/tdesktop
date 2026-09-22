/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_box.h"

#include "data/data_cloud_file.h"
#include "data/data_file_origin.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "storage/file_download.h"
#include "ui/controls/button_busy.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "wallet/wallet_content.h"

#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

using Phase = TonConnectBoxPhase;

struct State {
	std::optional<TonConnectBoxState> built;
	QString iconUrl;
	QImage icon;
	Data::CloudFile iconFile;
	QPointer<Ui::VerticalLayout> body;
	QPointer<Ui::RpWidget> iconRow;
	QPointer<Ui::FlatLabel> title;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> domain;
	QPointer<Ui::RpWidget> spinner;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> proof;
	QPointer<Ui::SlideWrap<Ui::FlatLabel>> error;
	QPointer<Ui::FlatLabel> notice;
	QPointer<Ui::RoundButton> connect;
	bool busy = false;
};

[[nodiscard]] bool SameBody(
		const TonConnectBoxState &built,
		const TonConnectBoxState &now) {
	return (built.phase == Phase::Notice) == (now.phase == Phase::Notice);
}

[[nodiscard]] QImage PrepareIcon(QImage image) {
	if (image.isNull()
		|| image.width() * 20 < image.height()
		|| image.height() * 20 < image.width()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	const auto full = st::walletConnectIconSize * ratio;
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
		not_null<State*> state,
		not_null<Main::Session*> session,
		Fn<void()> repaint) {
	state->iconFile.clear();
	state->icon = QImage();
	if (state->iconUrl.isEmpty()) {
		repaint();
		return;
	}
	state->iconFile.location = ImageLocation(
		DownloadLocation{ PlainUrlLocation{ state->iconUrl } },
		0,
		0);
	Data::LoadCloudFile(
		session,
		state->iconFile,
		Data::FileOrigin(),
		LoadFromCloudOrLocal,
		false,
		0,
		nullptr,
		[=](QImage image, QByteArray) {
			state->icon = PrepareIcon(std::move(image));
			repaint();
		},
		[=](bool) {
			repaint();
		});
}

void FillHeader(not_null<Ui::GenericBox*> box, not_null<State*> state) {
	const auto content = box->verticalLayout();
	const auto row = content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::walletConnectIconSize),
		st::walletConnectIconMargin);
	state->iconRow = row;
	row->paintRequest(
	) | rpl::on_next([=](QRect clip) {
		const auto size = row->height();
		const auto disc = QRect((row->width() - size) / 2, 0, size, size);
		if (!disc.intersects(clip)) {
			return;
		}
		auto p = QPainter(row);
		auto hq = PainterHighQualityEnabler(p);
		if (state->icon.isNull()) {
			p.setBrush(st::windowBgOver);
			p.setPen(Qt::NoPen);
			p.drawEllipse(disc);
			st::walletConnectIconPlaceholder.paintInCenter(p, disc);
		} else {
			p.drawImage(disc, state->icon);
		}
	}, row->lifetime());

	const auto &loading = st::walletBusyBoxLoading;
	const auto side = loading.size.height() + 2 * loading.thickness;
	const auto indicator = Info::Statistics::InfiniteRadialAnimationWidget(
		row,
		side,
		&loading);
	Info::Statistics::AddChildToWidgetCenter(row, indicator);
	state->spinner = indicator;

	state->title = content->add(
		object_ptr<Ui::FlatLabel>(content, st::walletConnectTitleLabel),
		st::walletConnectTitleMargin,
		style::al_top);
	state->domain = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				st::walletConnectDomainLabel),
			st::walletConnectDomainMargin),
		style::margins(),
		style::al_top);
}

[[nodiscard]] not_null<Ui::RpWidget*> AddButtonsRow(
		not_null<Ui::VerticalLayout*> content) {
	return content->add(
		object_ptr<Ui::FixedHeightWidget>(
			content,
			st::walletSendButton.height),
		st::walletConnectButtonsMargin);
}

void FillBody(
		not_null<Ui::GenericBox*> box,
		not_null<State*> state,
		const std::shared_ptr<Main::SessionShow> &show,
		const TonConnectBoxState &now,
		Fn<void()> connect) {
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
		const auto row = AddButtonsRow(content);
		const auto close = Ui::CreateChild<Ui::RoundButton>(
			row.get(),
			tr::lng_close(),
			st::walletSendButton);
		close->setClickedCallback([=] { box->closeBox(); });
		close->show();
		row->widthValue(
		) | rpl::on_next([=](int width) {
			close->setFullWidth(width);
			close->moveToLeft(0, 0, width);
		}, row->lifetime());
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
		MakeWalletCard(content, show),
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
	const auto row = AddButtonsRow(content);
	const auto cancel = Ui::CreateChild<Ui::RoundButton>(
		row.get(),
		tr::lng_cancel(),
		st::walletConnectCancelButton);
	cancel->setClickedCallback([=] { box->closeBox(); });
	cancel->show();
	const auto confirm = Ui::CreateChild<Ui::RoundButton>(
		row.get(),
		tr::lng_wallet_connect_button(),
		st::walletSendButton);
	confirm->setClickedCallback([=] {
		if (!state->busy) {
			connect();
		}
	});
	confirm->show();
	state->connect = confirm;
	row->widthValue(
	) | rpl::on_next([=](int width) {
		const auto single = (width - st::walletButtonsSkip) / 2;
		cancel->setFullWidth(single);
		confirm->setFullWidth(single);
		cancel->moveToLeft(0, 0, width);
		confirm->moveToRight(0, 0, width);
	}, row->lifetime());
}

void UpdateState(
		not_null<State*> state,
		not_null<Main::Session*> session,
		const TonConnectBoxState &now,
		anim::type animated) {
	const auto loading = (now.phase == Phase::Loading);
	state->title->setText(loading
		? tr::lng_wallet_connect_loading(tr::now)
		: now.name.isEmpty()
		? tr::lng_wallet_connect_title(tr::now)
		: tr::lng_wallet_connect_title_app(tr::now, lt_name, now.name));
	const auto domain = state->domain.data();
	if (!now.domain.isEmpty()) {
		domain->entity()->setText(now.domain);
	}
	domain->toggle(!now.domain.isEmpty(), animated);
	state->spinner->setVisible(loading);
	if (state->iconUrl != now.iconUrl) {
		state->iconUrl = now.iconUrl;
		LoadIcon(state, session, [=] {
			if (const auto row = state->iconRow.data()) {
				row->update();
			}
		});
	}
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
	state->busy = (now.phase != Phase::Confirm);
	Ui::SetButtonBusy(state->connect.data(), state->busy);
}

} // namespace

void TonConnectBox(not_null<Ui::GenericBox*> box, TonConnectBoxArgs args) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletConnectBox);
	box->setNoContentMargin(true);
	box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

	const auto state = box->lifetime().make_state<State>();
	const auto reported = std::make_shared<bool>(false);
	const auto dismiss = [=, dismissed = args.dismissed] {
		if (!std::exchange(*reported, true) && dismissed) {
			dismissed();
		}
	};
	box->boxClosing() | rpl::on_next(dismiss, box->lifetime());
	box->lifetime().add(dismiss);

	FillHeader(box, state);
	state->body = box->verticalLayout()->add(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);

	const auto show = args.show;
	std::move(
		args.state
	) | rpl::on_next([=, connect = args.connect](
			const TonConnectBoxState &now) {
		const auto rebuild = !state->built || !SameBody(*state->built, now);
		if (rebuild) {
			FillBody(box, state, show, now, connect);
		}
		UpdateState(
			state,
			&show->session(),
			now,
			rebuild ? anim::type::instant : anim::type::normal);
		state->built = now;
	}, box->lifetime());
}

} // namespace Wallet

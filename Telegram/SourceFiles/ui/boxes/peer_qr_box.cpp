/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/peer_qr_box.h"

#include "base/weak_qptr.h"
#include "boxes/share_box.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_wall_paper.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/effects/peer_qr_surface.h"
#include "ui/effects/radial_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>

namespace Ui {
namespace {

struct BoxState {
	QString link;
	QString username;
	QImage image;
	QString imageLink;
	QString imageUsername;
	bool rendering = false;
};

class PeerQrButton final : public IconButton {
public:
	PeerQrButton(QWidget *parent, const style::IconButton &st);

	void setColors(QColor disc, QColor icon);
	void setBusy(bool busy);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	InfiniteRadialAnimation _loading;
	QColor _disc;
	QColor _icon;
	bool _busy = false;

};

PeerQrButton::PeerQrButton(QWidget *parent, const style::IconButton &st)
: IconButton(parent, st)
, _loading([=] { update(); }, st::peerQrButtonLoading) {
}

void PeerQrButton::setColors(QColor disc, QColor icon) {
	_disc = disc;
	_icon = icon;
	setIconColorOverride(icon);
	update();
}

void PeerQrButton::setBusy(bool busy) {
	if (_busy == busy) {
		return;
	}
	_busy = busy;
	if (busy) {
		_loading.start();
	} else {
		_loading.stop(anim::type::instant);
	}
	update();
}

void PeerQrButton::paintEvent(QPaintEvent *e) {
	const auto disc = QRect(
		st().rippleAreaPosition,
		QSize(st().rippleAreaSize, st().rippleAreaSize));
	{
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(_disc);
		p.drawEllipse(disc);
		if (_busy) {
			const auto &loading = st::peerQrButtonLoading;
			const auto size = loading.size;
			InfiniteRadialAnimation::Draw(
				p,
				_loading.computeState(),
				disc.topLeft() + QPoint(
					(disc.width() - size.width()) / 2,
					(disc.height() - size.height()) / 2),
				size,
				width(),
				_icon,
				loading.thickness);
			return;
		}
	}
	IconButton::paintEvent(e);
}

[[nodiscard]] PeerQrAppearance DefaultAppearance() {
	const auto paper = Data::WallPaper::ConstructDefault();
	auto colors = paper.backgroundColors();
	const auto white = QColor(255, 255, 255);
	const auto darkness = [&](const QColor &color) {
		return CountContrast(color, white);
	};
	const auto edge = colors.empty()
		? QColor()
		: *ranges::max_element(colors, ranges::less(), darkness);
	return {
		.colors = std::move(colors),
		.patternPath = u":/gui/art/background.tgv"_q,
		.patternOpacity = paper.patternOpacity(),
		.edge = edge,
	};
}

[[nodiscard]] rpl::producer<QString> QrLinkValue(
		PeerData *peer,
		const std::optional<QString> &customLink) {
	if (customLink) {
		return rpl::single(*customLink);
	} else if (peer) {
		return Info::Profile::LinkValue(
			peer,
			true
		) | rpl::map([](const Info::Profile::LinkWithUrl &link) {
			return link.text;
		});
	}
	return rpl::single(QString());
}

[[nodiscard]] not_null<PeerQrButton*> AddButton(
		not_null<PeerQrSurface*> surface,
		const style::IconButton &st,
		const QString &name) {
	const auto result = CreateChild<PeerQrButton>(surface.get(), st);
	const auto &colors = surface->colors();
	result->setColors(colors.control, colors.foreground);
	result->setAccessibleName(name);
	return result;
}

[[nodiscard]] not_null<FlatLabel*> AddLabel(
		not_null<PeerQrSurface*> surface,
		const style::FlatLabel &st) {
	const auto result = CreateChild<FlatLabel>(surface.get(), st);
	result->setTextColorOverride(surface->colors().foreground);
	return result;
}

void RenderCardImage(
		not_null<GenericBox*> box,
		not_null<PeerQrSurface*> surface,
		not_null<BoxState*> state,
		not_null<PeerQrButton*> button,
		Fn<void(QImage)> done) {
	if (state->rendering) {
		return;
	} else if (!state->image.isNull()
		&& state->imageLink == state->link
		&& state->imageUsername == state->username) {
		done(state->image);
		return;
	}
	state->rendering = true;
	button->setBusy(true);
	const auto link = state->link;
	const auto username = state->username;
	const auto weak = base::make_weak(box);
	crl::async([=, args = surface->prepareImage()]() mutable {
		auto image = RenderPeerQrImage(std::move(args));
		crl::on_main(weak, [=, image = std::move(image)] {
			state->rendering = false;
			button->setBusy(false);
			state->image = image;
			state->imageLink = link;
			state->imageUsername = username;
			done(image);
		});
	});
}

} // namespace

void FillPeerQrBox(
		not_null<Ui::GenericBox*> box,
		PeerData *peer,
		std::optional<QString> customLink,
		rpl::producer<QString> about,
		rpl::producer<QString> title) {
	box->setStyle(st::peerQrBox);
	box->setWidth(st::peerQrBoxWidth);
	box->setNoContentMargin(true);
	box->setCustomCornersFilling(RectPart::FullTop | RectPart::FullBottom);
	box->verticalLayout()->resizeToWidth(st::peerQrBoxWidth);

	const auto peerBox = peer && !customLink;
	const auto state = box->lifetime().make_state<BoxState>();
	const auto surface = box->addRow(
		object_ptr<PeerQrSurface>(box, DefaultAppearance()),
		style::margins());
	const auto close = AddButton(
		surface,
		st::peerQrClose,
		tr::lng_close(tr::now));
	const auto copy = AddButton(
		surface,
		st::peerQrCopy,
		tr::lng_group_invite_context_copy(tr::now));
	const auto window = Core::App().findWindow(box);
	const auto controller = window ? window->sessionController() : nullptr;
	const auto weakController = base::make_weak(controller);
	const auto share = controller
		? AddButton(
			surface,
			st::peerQrShare,
			tr::lng_group_invite_context_share(tr::now)).get()
		: nullptr;
	const auto titleLabel = title
		? AddLabel(surface, st::peerQrTitle).get()
		: nullptr;
	const auto aboutLabel = about
		? AddLabel(surface, st::peerQrAbout).get()
		: nullptr;
	if (aboutLabel) {
		aboutLabel->setTryMakeSimilarLines(true);
	}

	if (peerBox) {
		Info::Profile::UsernameValue(
			peer,
			true
		) | rpl::on_next([=](const TextWithEntities &username) {
			state->username = username.text.toUpper();
			surface->setUsername(state->username);
		}, box->lifetime());
	}
	QrLinkValue(
		peer,
		customLink
	) | rpl::on_next([=](const QString &link) {
		state->link = link;
		surface->setLink(link);
		if (link.isEmpty()) {
			box->showFinishes() | rpl::on_next([=] {
				box->closeBox();
			}, box->lifetime());
			box->closeBox();
		}
	}, box->lifetime());

	const auto relayout = [=] {
		const auto width = surface->width();
		const auto skip = st::peerQrButtonSkip;
		const auto cardWidth = st::peerQrCardWidth;
		const auto card = QRect(
			(width - cardWidth) / 2,
			skip + close->height() + st::peerQrCardSkip,
			cardWidth,
			PeerQrCardHeight(cardWidth, peerBox));
		auto bottom = card.y() + card.height();
		if (peerBox) {
			bottom += st::peerQrPillSkip + st::peerQrPillHeight;
		}
		if (aboutLabel && !aboutLabel->isHidden()) {
			aboutLabel->resizeToWidth(cardWidth);
			aboutLabel->moveToLeft(
				card.x(),
				bottom + st::peerQrPillSkip,
				width);
			bottom += st::peerQrPillSkip + aboutLabel->height();
		}
		const auto rowTop = bottom + st::peerQrCardSkip;
		close->moveToRight(skip, skip, width);
		copy->moveToLeft(skip, rowTop, width);
		if (share) {
			share->moveToRight(skip, rowTop, width);
		}
		if (titleLabel) {
			const auto left = st::boxTitlePosition.x();
			titleLabel->resizeToNaturalWidth(
				width - left - close->width() - 2 * skip);
			titleLabel->moveToLeft(
				left,
				skip + (close->height() - titleLabel->height()) / 2,
				width);
		}
		surface->setCardGeometry(card);
		surface->resize(width, rowTop + copy->height() + skip);
	};
	if (titleLabel) {
		std::move(title) | rpl::on_next([=](const QString &text) {
			titleLabel->setText(text);
			relayout();
		}, titleLabel->lifetime());
	}
	if (aboutLabel) {
		std::move(about) | rpl::on_next([=](const QString &text) {
			aboutLabel->setText(text);
			aboutLabel->setVisible(!text.isEmpty());
			relayout();
		}, aboutLabel->lifetime());
	}
	surface->widthValue() | rpl::on_next(relayout, surface->lifetime());

	close->setClickedCallback([=] {
		box->closeBox();
	});
	copy->setClickedCallback([=] {
		RenderCardImage(box, surface, state, copy, [=](QImage image) {
			auto mime = std::make_unique<QMimeData>();
			mime->setImageData(std::move(image));
			QGuiApplication::clipboard()->setMimeData(mime.release());
			box->uiShow()->showToast({
				.text = { tr::lng_group_invite_qr_copied(tr::now) },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		});
	});
	if (share) {
		share->setClickedCallback([=] {
			RenderCardImage(box, surface, state, share, [=](QImage image) {
				if (const auto strong = weakController.get()) {
					FastShareImage(
						strong->uiShow(),
						std::move(image),
						state->imageLink);
				}
			});
		});
	}

	box->showFinishes(
	) | rpl::take(1) | rpl::on_next([=] {
		surface->startAnimations();
		const auto strong = weakController.get();
		if (share && strong) {
			strong->checkHighlightControl(
				u"self-qr-code/share"_q,
				share,
				{ .rippleShape = true, .scroll = false });
		}
	}, box->lifetime());
}

void DefaultShowFillPeerQrBoxCallback(
		std::shared_ptr<Ui::Show> show,
		PeerData *peer) {
	if (peer && !peer->username().isEmpty()) {
		show->show(Box(
			Ui::FillPeerQrBox,
			peer,
			std::nullopt,
			nullptr,
			nullptr));
	}
}

} // namespace Ui

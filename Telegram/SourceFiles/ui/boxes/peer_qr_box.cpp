/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/peer_qr_box.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "base/weak_qptr.h"
#include "boxes/share_box.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_wall_paper.h"
#include "info/profile/info_profile_free_codes.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/effects/peer_qr_pill.h"
#include "ui/effects/peer_qr_surface.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/color_contrast.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>
#include <QtGui/QtEvents>

namespace Ui {
namespace {

constexpr auto kToastDuration = crl::time(2800);
constexpr auto kShareCutoff = crl::time(3000);

enum class FreeMode {
	Fee,
	Temporary,
	Permanent,
};

struct FreeCodesState {
	FreeMode mode = FreeMode::Fee;
	PeerQrPill *pill = nullptr;
	int price = 0;
	uint32 generation = 0;
	bool requesting = false;
	TimeId expires = 0;
	TimeId shareExpires = 0;
	crl::time arrival = 0;
	crl::time deadline = 0;
	crl::time shareDeadline = 0;
	base::Timer expireTimer;
	base::Timer shareCutoffTimer;
	base::unique_qptr<PopupMenu> menu;
	base::weak_ptr<Toast::Instance> toast;
};

struct BoxState {
	QString link;
	QString plainLink;
	QString username;
	QImage image;
	QString imageLink;
	QString imageUsername;
	base::weak_qptr<BoxContent> shareBox;
	FreeCodesState free;
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

class AboutAction final : public Menu::ItemBase {
public:
	AboutAction(
		not_null<Menu::Menu*> parent,
		const style::Menu &st,
		const QString &text,
		const QString &about,
		const style::icon *icon,
		Fn<void()> callback);

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QImage prepareRippleMask() const override;
	int contentHeight() const override;

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const style::icon *_icon = nullptr;
	const QString _text;
	const QString _about;
	const int _height = 0;

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

AboutAction::AboutAction(
	not_null<Menu::Menu*> parent,
	const style::Menu &st,
	const QString &text,
	const QString &about,
	const style::icon *icon,
	Fn<void()> callback)
: ItemBase(parent, st)
, _dummyAction(CreateChild<QAction>(parent))
, _st(st)
, _icon(icon)
, _text(text)
, _about(about)
, _height(st.itemPadding.top()
	+ st.itemStyle.font->height
	+ st::peerQrMenuAboutFont->height
	+ st.itemPadding.bottom()) {
	fitToMenuWidth();
	setActionTriggered(std::move(callback));
	enableMouseSelecting();

	const auto &padding = _st.itemPadding;
	const auto textWidth = std::max(
		_st.itemStyle.font->width(_text),
		st::peerQrMenuAboutFont->width(_about));
	setMinWidth(std::clamp(
		padding.left() + textWidth + padding.right(),
		_st.widthMin,
		_st.widthMax));
}

QAccessible::Role AboutAction::accessibilityRole() {
	return QAccessible::MenuItem;
}

QString AboutAction::accessibilityName() {
	return _text + u", "_q + _about;
}

bool AboutAction::isEnabled() const {
	return true;
}

not_null<QAction*> AboutAction::action() const {
	return _dummyAction;
}

void AboutAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Menu::TriggeredSource::Keyboard);
	}
}

void AboutAction::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto selected = isSelected();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), _height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), _height, selected ? _st.itemBgOver : _st.itemBg);
	paintRipple(p, 0, 0);
	if (_icon) {
		_icon->paint(
			p,
			_st.itemIconPosition.x(),
			(_height - _icon->height()) / 2,
			width());
	}
	const auto &padding = _st.itemPadding;
	const auto available = width() - padding.left() - padding.right();
	const auto &font = _st.itemStyle.font;
	p.setFont(font);
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	p.drawTextLeft(
		padding.left(),
		padding.top(),
		width(),
		font->elided(_text, available));
	const auto &aboutFont = st::peerQrMenuAboutFont;
	p.setFont(aboutFont);
	p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
	p.drawTextLeft(
		padding.left(),
		padding.top() + font->height,
		width(),
		aboutFont->elided(_about, available));
}

QImage AboutAction::prepareRippleMask() const {
	return RippleAnimation::RectMask(size());
}

int AboutAction::contentHeight() const {
	return _height;
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

void ShowFreeToast(
		not_null<QWidget*> parent,
		not_null<BoxState*> state,
		Toast::Config &&config) {
	if (const auto previous = state->free.toast.get()) {
		previous->hideAnimated();
	}
	config.st = &st::defaultToast;
	config.attach = RectPart::Bottom;
	config.singleline = true;
	config.duration = kToastDuration;
	state->free.toast = Toast::Show(parent, std::move(config));
}

[[nodiscard]] QString ExpiresToastText(crl::time remaining) {
	const auto seconds = std::max(int((remaining + 999) / 1000), 0);
	return tr::lng_no_paid_token_toast_expires(
		tr::now,
		lt_duration,
		((seconds >= 60)
			? tr::lng_minutes(tr::now, lt_count, seconds / 60)
			: tr::lng_seconds(tr::now, lt_count, seconds)));
}

[[nodiscard]] crl::time DeadlineFor(TimeId expires, crl::time now) {
	return now + (expires - base::unixtime::now()) * crl::time(1000);
}

void ShowExpiringToast(
		not_null<QWidget*> parent,
		not_null<BoxState*> state) {
	ShowFreeToast(parent, state, {
		.text = tr::lng_no_paid_token_toast_expiring(tr::now, tr::marked),
		.icon = &st::peerQrToastTimer,
	});
}

[[nodiscard]] bool RefuseExpiring(
		not_null<QWidget*> parent,
		not_null<BoxState*> state) {
	const auto &free = state->free;
	if (free.mode != FreeMode::Temporary
		|| free.deadline - crl::now() > kShareCutoff) {
		return false;
	}
	ShowExpiringToast(parent, state);
	return true;
}

void CloseShareBox(not_null<QWidget*> parent, not_null<BoxState*> state) {
	auto &free = state->free;
	free.shareExpires = 0;
	free.shareCutoffTimer.cancel();
	if (const auto strong = state->shareBox.get()) {
		strong->closeBox();
		ShowExpiringToast(parent, state);
	}
}

void ScheduleShareCutoff(
		not_null<QWidget*> parent,
		not_null<BoxState*> state) {
	auto &free = state->free;
	if (!free.shareExpires || !state->shareBox) {
		free.shareExpires = 0;
		free.shareCutoffTimer.cancel();
		return;
	}
	const auto left = free.shareDeadline - kShareCutoff - crl::now();
	if (left > 0) {
		free.shareCutoffTimer.callOnce(left, Qt::PreciseTimer);
	} else {
		CloseShareBox(parent, state);
	}
}

void TrackShareBox(
		not_null<QWidget*> parent,
		not_null<BoxState*> state,
		base::weak_qptr<BoxContent> box) {
	auto &free = state->free;
	const auto temporary = (free.mode == FreeMode::Temporary);
	state->shareBox = std::move(box);
	free.shareExpires = temporary ? free.expires : TimeId();
	free.shareDeadline = temporary ? free.deadline : crl::time();
	ScheduleShareCutoff(parent, state);
}

void SetupFreeCodes(
		not_null<GenericBox*> box,
		not_null<PeerQrSurface*> surface,
		not_null<BoxState*> state,
		not_null<PeerData*> peer) {
	using Request = Info::Profile::FreeCodeRequest;
	const auto free = &state->free;
	const auto pill = CreateChild<PeerQrPill>(
		surface.get(),
		surface->colors());
	free->pill = pill;

	const auto applyLink = [=](const QString &link) {
		state->link = link;
		surface->setLink(link, anim::type::normal);
	};
	const auto scheduleExpiry = [=] {
		free->expireTimer.callOnce(
			std::max(free->deadline - crl::now(), crl::time()),
			Qt::PreciseTimer);
	};
	const auto dropPending = [=] {
		++free->generation;
		free->requesting = false;
		free->expireTimer.cancel();
		if (const auto menu = free->menu.get()) {
			menu->hideMenu(true);
		}
	};
	const auto applyCode = [=](
			Request what,
			const Info::Profile::FreeCode &code) {
		const auto now = crl::now();
		if (what == Request::Temporary || what == Request::ResetTemporary) {
			free->mode = FreeMode::Temporary;
			free->expires = code.expires;
			free->arrival = now;
			free->deadline = DeadlineFor(code.expires, now);
			pill->showTemporary(free->arrival, free->deadline);
			applyLink(code.link);
			scheduleExpiry();
			ShowFreeToast(surface, state, {
				.text = tr::marked(ExpiresToastText(free->deadline - now)),
				.icon = &st::peerQrToastTimer,
			});
			return;
		}
		const auto reset = (what == Request::ResetPermanent);
		free->mode = FreeMode::Permanent;
		free->expireTimer.cancel();
		pill->showPermanent();
		applyLink(code.link);
		ShowFreeToast(surface, state, {
			.text = (reset
				? tr::lng_no_paid_token_toast_reset(tr::now, tr::marked)
				: tr::lng_no_paid_token_toast_permanent(tr::now, tr::marked)),
			.icon = (reset ? &st::peerQrToastCheck : &st::peerQrToastTimer),
		});
	};
	const auto request = [=](Request what) {
		if (free->requesting) {
			return;
		}
		free->requesting = true;
		const auto generation = free->generation;
		const auto current = [=] {
			if (free->generation != generation) {
				return false;
			}
			free->requesting = false;
			return true;
		};
		Info::Profile::RequestFreeMessagesCode(
			&peer->session(),
			what,
			crl::guard(box, [=](Info::Profile::FreeCode code) {
				if (current()) {
					applyCode(what, code);
				}
			}),
			crl::guard(box, [=](QString error) {
				if (current() && !error.isEmpty()) {
					ShowFreeToast(surface, state, {
						.text = tr::marked(error),
					});
				}
			}));
	};
	const auto returnToFee = [=] {
		dropPending();
		free->mode = FreeMode::Fee;
		pill->showFee(free->price);
		applyLink(state->plainLink);
	};
	const auto showMenu = [=] {
		const auto temporary = (free->mode == FreeMode::Temporary);
		free->menu = base::make_unique_q<PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto menu = free->menu.get();
		menu->addAction(
			tr::lng_no_paid_token_menu_reset(tr::now),
			[=] {
				request(temporary
					? Request::ResetTemporary
					: Request::ResetPermanent);
			},
			&st::peerQrMenuReset);
		menu->addAction(base::make_unique_q<AboutAction>(
			menu->menu(),
			menu->st().menu,
			(temporary
				? tr::lng_no_paid_token_menu_disable_timer(tr::now)
				: tr::lng_no_paid_token_menu_enable_timer(tr::now)),
			(temporary
				? tr::lng_no_paid_token_menu_disable_timer_about(tr::now)
				: tr::lng_no_paid_token_menu_enable_timer_about(tr::now)),
			(temporary ? &st::peerQrMenuTimerOff : &st::menuIconTimer),
			[=] {
				request(temporary ? Request::Permanent : Request::Temporary);
			}));
		menu->addAction(
			tr::lng_no_paid_token_menu_restore(tr::now),
			[=] {
				returnToFee();
				ShowFreeToast(surface, state, {
					.text = tr::lng_no_paid_token_toast_restored(
						tr::now,
						tr::marked),
					.iconLottie = u"toast/star_premium_2"_q,
					.iconLottieSize = st::toastLottieIconSize,
				});
			},
			&st::menuIconStar);
		menu->setForcedVerticalOrigin(PopupMenu::VerticalOrigin::Top);
		const auto inner = menu->inner();
		const auto geometry = pill->pillGeometry();
		menu->popup(surface->mapToGlobal(QPoint(
			geometry.x() + (geometry.width() - inner.width()) / 2 - inner.x(),
			geometry.y())));
	};

	free->expireTimer.setCallback([=] {
		if (free->mode == FreeMode::Temporary) {
			returnToFee();
			ShowFreeToast(surface, state, {
				.text = tr::lng_no_paid_token_toast_expired(
					tr::now,
					tr::marked),
				.icon = &st::peerQrToastTimer,
			});
		}
	});
	free->shareCutoffTimer.setCallback([=] {
		CloseShareBox(surface, state);
	});
	base::unixtime::updates() | rpl::on_next([=] {
		const auto now = crl::now();
		if (free->mode == FreeMode::Temporary) {
			free->deadline = DeadlineFor(free->expires, now);
			pill->setDeadline(free->deadline);
			scheduleExpiry();
		}
		if (free->shareExpires) {
			free->shareDeadline = DeadlineFor(free->shareExpires, now);
			ScheduleShareCutoff(surface, state);
		}
	}, box->lifetime());

	Info::Profile::FreeMessagesPriceValue(
		&peer->session()
	) | rpl::on_next([=](int price) {
		free->price = price;
		if (price > 0) {
			if (free->mode == FreeMode::Fee) {
				pill->showFee(price);
			}
			return;
		}
		dropPending();
		if (free->mode != FreeMode::Fee) {
			free->mode = FreeMode::Fee;
			applyLink(state->plainLink);
		}
		pill->hidePill();
	}, box->lifetime());

	pill->clicks() | rpl::on_next([=] {
		if (free->requesting) {
			return;
		} else if (free->mode == FreeMode::Fee) {
			request(Request::Temporary);
		} else if (!free->menu) {
			showMenu();
		}
	}, pill->lifetime());

	box->lifetime().add([=] {
		// the cut-off timer dies with the box, so must the share box it guards
		if (free->shareExpires) {
			if (const auto strong = state->shareBox.get()) {
				strong->closeBox();
			}
		}
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
	if (peerBox && peer->isSelf()) {
		SetupFreeCodes(box, surface, state, peer);
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
		state->plainLink = link;
		if (state->free.mode == FreeMode::Fee) {
			state->link = link;
			surface->setLink(link);
		}
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
		if (const auto pill = state->free.pill) {
			pill->setPillPosition(width / 2, bottom + st::peerQrPillSkip);
		}
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
	const auto refuseImage = [=] {
		return state->free.requesting || RefuseExpiring(surface, state);
	};
	const auto dropRendered = [=] {
		return refuseImage() || (state->imageLink != state->link);
	};
	copy->setClickedCallback([=] {
		if (refuseImage()) {
			return;
		}
		RenderCardImage(box, surface, state, copy, [=](QImage image) {
			if (dropRendered()) {
				return;
			}
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
			if (refuseImage()) {
				return;
			}
			RenderCardImage(box, surface, state, share, [=](QImage image) {
				if (dropRendered()) {
					return;
				}
				if (const auto strong = weakController.get()) {
					TrackShareBox(
						surface,
						state,
						FastShareImage(
							strong->uiShow(),
							std::move(image),
							state->imageLink));
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

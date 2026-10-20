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
#include "info/profile/info_profile_free_codes.h"
#include "info/profile/info_profile_qr_emoji.h"
#include "info/profile/info_profile_qr_gifts.h"
#include "info/profile/info_profile_qr_link.h"
#include "info/profile/info_profile_share_theme.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "ui/effects/peer_qr_colors_panel.h"
#include "ui/effects/peer_qr_gifts_panel.h"
#include "ui/effects/peer_qr_panel.h"
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
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QDateTime>
#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>
#include <QtGui/QtEvents>

namespace Ui {
namespace {

constexpr auto kToastDuration = crl::time(2800);
constexpr auto kShareCutoff = crl::time(3000);
constexpr auto kMaxEmojiPicks = 6;

enum class FreeMode {
	Fee,
	Temporary,
	Permanent,
};

struct LinkExpiry {
	TimeId expires = 0;
	crl::time deadline = 0;
	bool freeCode = false;
};

struct FreeCodesState {
	FreeMode mode = FreeMode::Fee;
	PeerQrPill *pill = nullptr;
	int price = 0;
	uint32 generation = 0;
	uint32 menuGeneration = 0;
	std::optional<Info::Profile::FreeCodeRequest> requesting;
	TimeId expires = 0;
	crl::time arrival = 0;
	crl::time deadline = 0;
	base::Timer expireTimer;
	base::unique_qptr<PopupMenu> menu;
	base::weak_ptr<Toast::Instance> toast;
};

class PeerQrButton;

struct AppearanceState {
	PeerQrPanel *colorsPanel = nullptr;
	PeerQrColorsPanel *colors = nullptr;
	PeerQrPanel *emojiPanel = nullptr;
	PeerQrPanel *giftsPanel = nullptr;
	PeerQrGiftsPanel *gifts = nullptr;
	std::optional<Info::Profile::QrEmojiPicker> picker;
	Fn<void(
		const PeerQrThemeKey&,
		Fn<void()>,
		Fn<void(QString)>)> saveGift;
	RpWidget *island = nullptr;
	std::vector<not_null<PeerQrButton*>> controls;
	std::vector<PeerQrSwatch> swatches;
	std::vector<QString> emojiPicks;
	std::optional<PeerQrSwatch> current;
	std::optional<PeerQrSwatch> served;
	std::optional<PeerQrSwatch> chosen;
	std::optional<PeerQrThemeKey> saving;
	std::optional<PeerQrSwatch> pending;
	bool canSave = false;
	bool panelShown = false;
	bool shown = false;
	bool giftsActive = false;
	float64 progress = 0.;
};

struct OpenLayout {
	QRect panel;
	QPointF card;
	float64 scale = 1.;
	int pillTop = 0;
};

struct ImageSnapshot {
	QString link;
	QString username;
	Info::Profile::OwnQrLinkKind kind = Info::Profile::OwnQrLinkKind::Username;
	FreeMode mode = FreeMode::Fee;
	uint32 generation = 0;
	uint32 clockGeneration = 0;
	LinkExpiry expiry;
	PeerQrAppearance appearance;
	QColor ink;
	std::vector<QImage> stills;
	QPointF offset;
};

struct BoxState {
	QString link;
	QString plainLink;
	QString username;
	Info::Profile::OwnQrLinkKind kind = Info::Profile::OwnQrLinkKind::Username;
	TimeId plainExpires = 0;
	crl::time plainDeadline = 0;
	uint32 clockGeneration = 0;
	rpl::variable<bool> temporaryOnly = false;
	QImage image;
	ImageSnapshot imageSnapshot;
	base::weak_qptr<BoxContent> shareBox;
	LinkExpiry shareExpiry;
	base::Timer shareCutoffTimer;
	FreeCodesState free;
	AppearanceState appearance;
	bool rendering = false;
};

class PeerQrButton final : public IconButton {
public:
	PeerQrButton(QWidget *parent, const style::IconButton &st);

	void setColors(QColor disc, QColor icon);
	void setBusy(bool busy);
	void setOpacity(float64 opacity);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	InfiniteRadialAnimation _loading;
	QColor _disc;
	QColor _icon;
	float64 _opacity = 1.;
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

void PeerQrButton::setOpacity(float64 opacity) {
	if (_opacity == opacity) {
		return;
	}
	_opacity = opacity;
	update();
}

void PeerQrButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.setOpacity(_opacity);
	const auto disc = rect();
	{
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = disc.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(_disc);
		p.drawRoundedRect(disc, radius, radius);
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
	paintRipple(p, st().rippleAreaPosition);
	st().icon.paintInCenter(p, disc, _icon);
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
	surface->colorsValue() | rpl::on_next([=](const PeerQrColors &colors) {
		result->setColors(colors.control, colors.foreground);
	}, result->lifetime());
	result->setAccessibleName(name);
	return result;
}

[[nodiscard]] not_null<RpWidget*> AddIsland(
		not_null<PeerQrSurface*> surface,
		not_null<const AppearanceState*> appearance,
		int buttons) {
	const auto result = CreateChild<RpWidget>(surface.get());
	result->resize(
		buttons * st::peerQrColors.width,
		st::peerQrButton.height);
	surface->colorsValue() | rpl::on_next([=] {
		result->update();
	}, result->lifetime());
	result->paintRequest() | rpl::on_next([=] {
		// a panel's glass grows from this pill and takes its place
		if (appearance->panelShown) {
			return;
		}
		auto p = QPainter(result);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = result->height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(surface->colors().control);
		p.drawRoundedRect(result->rect(), radius, radius);
	}, result->lifetime());
	return result;
}

[[nodiscard]] not_null<PeerQrButton*> AddIslandButton(
		not_null<RpWidget*> island,
		not_null<AppearanceState*> appearance,
		not_null<PeerQrSurface*> surface,
		int index,
		const style::IconButton &st,
		const QString &name) {
	const auto result = CreateChild<PeerQrButton>(island.get(), st);
	result->moveToLeft(
		index * st::peerQrColors.width,
		0,
		island->width());
	surface->colorsValue() | rpl::on_next([=](const PeerQrColors &colors) {
		result->setColors(QColor(Qt::transparent), colors.foreground);
	}, result->lifetime());
	result->setAccessibleName(name);
	appearance->controls.push_back(result);
	return result;
}

[[nodiscard]] not_null<FlatLabel*> AddLabel(
		not_null<PeerQrSurface*> surface,
		const style::FlatLabel &st) {
	const auto result = CreateChild<FlatLabel>(surface.get(), st);
	surface->colorsValue() | rpl::on_next([=](const PeerQrColors &colors) {
		result->setTextColorOverride(colors.foreground);
	}, result->lifetime());
	return result;
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
	return now + base::unixtime::parse(expires).toMSecsSinceEpoch()
		- QDateTime::currentMSecsSinceEpoch();
}

[[nodiscard]] LinkExpiry EffectiveExpiry(not_null<BoxState*> state) {
	const auto &free = state->free;
	return (free.mode == FreeMode::Temporary)
		? LinkExpiry{ free.expires, free.deadline, true }
		: (free.mode == FreeMode::Fee)
		? LinkExpiry{ state->plainExpires, state->plainDeadline }
		: LinkExpiry();
}

void ShowExpiringToast(
		not_null<QWidget*> parent,
		not_null<BoxState*> state,
		bool freeCode) {
	ShowFreeToast(parent, state, {
		.text = (freeCode
			? tr::lng_no_paid_token_toast_expiring(tr::now, tr::marked)
			: tr::lng_peer_qr_link_expiring(tr::now, tr::marked)),
		.icon = &st::peerQrToastTimer,
	});
}

[[nodiscard]] bool RefuseExpiring(
		not_null<QWidget*> parent,
		not_null<BoxState*> state,
		const LinkExpiry &expiry) {
	if (!expiry.expires || expiry.deadline - crl::now() > kShareCutoff) {
		return false;
	}
	ShowExpiringToast(parent, state, expiry.freeCode);
	return true;
}

[[nodiscard]] bool RefuseImage(
		not_null<QWidget*> parent,
		not_null<BoxState*> state) {
	if (state->free.requesting) {
		return true;
	} else if (state->link.isEmpty()) {
		ShowFreeToast(parent, state, {
			.text = tr::lng_peer_qr_link_unavailable(tr::now, tr::marked),
		});
		return true;
	}
	return RefuseExpiring(parent, state, EffectiveExpiry(state));
}

void CloseShareBox(not_null<QWidget*> parent, not_null<BoxState*> state) {
	const auto expiry = base::take(state->shareExpiry);
	const auto strong = base::take(state->shareBox).get();
	state->shareCutoffTimer.cancel();
	if (strong) {
		strong->closeBox();
		ShowExpiringToast(parent, state, expiry.freeCode);
	}
}

void ScheduleShareCutoff(
		not_null<QWidget*> parent,
		not_null<BoxState*> state) {
	if (!state->shareExpiry.expires || !state->shareBox) {
		state->shareExpiry = {};
		state->shareCutoffTimer.cancel();
		return;
	}
	const auto left = state->shareExpiry.deadline - kShareCutoff - crl::now();
	if (left > 0) {
		state->shareCutoffTimer.callOnce(left, Qt::PreciseTimer);
	} else {
		CloseShareBox(parent, state);
	}
}

void TrackShareBox(
		not_null<QWidget*> parent,
		not_null<BoxState*> state,
		base::weak_qptr<BoxContent> box,
		LinkExpiry expiry) {
	state->shareBox = std::move(box);
	state->shareExpiry = expiry;
	ScheduleShareCutoff(parent, state);
}

[[nodiscard]] bool ImageIsCurrent(
		not_null<PeerQrSurface*> surface,
		not_null<BoxState*> state,
		const ImageSnapshot &snapshot) {
	return !snapshot.link.isEmpty()
		&& snapshot.link == state->link
		&& snapshot.kind == state->kind
		&& snapshot.mode == state->free.mode
		&& snapshot.generation == state->free.generation
		&& snapshot.expiry.expires == EffectiveExpiry(state).expires
		&& snapshot.appearance == surface->appearance();
}

void RenderCardImage(
		not_null<GenericBox*> box,
		not_null<PeerQrSurface*> surface,
		not_null<BoxState*> state,
		not_null<PeerQrButton*> button,
		Fn<void(QImage, const ImageSnapshot&)> done) {
	if (state->rendering || RefuseImage(surface, state)) {
		return;
	}
	auto args = surface->prepareImage();
	auto snapshot = ImageSnapshot{
		.link = state->link,
		.username = state->username,
		.kind = state->kind,
		.mode = state->free.mode,
		.generation = state->free.generation,
		.clockGeneration = state->clockGeneration,
		.expiry = EffectiveExpiry(state),
		.appearance = surface->appearance(),
		.ink = args.colors.ink,
		.stills = args.emoji.stills,
		.offset = args.layerOffset,
	};
	snapshot.stills.push_back(args.gift.model);
	snapshot.stills.push_back(args.gift.symbol);
	const auto &cached = state->imageSnapshot;
	if (!state->image.isNull()
		&& ImageIsCurrent(surface, state, cached)
		&& cached.username == snapshot.username
		&& cached.ink == snapshot.ink
		&& cached.stills == snapshot.stills
		&& cached.offset == snapshot.offset) {
		done(state->image, cached);
		return;
	}
	state->rendering = true;
	button->setBusy(true);
	const auto weak = base::make_weak(box);
	crl::async([=, args = std::move(args)]() mutable {
		auto image = RenderPeerQrImage(std::move(args));
		crl::on_main(weak, [=, image = std::move(image)] {
			state->rendering = false;
			button->setBusy(false);
			if (image.isNull()
				|| !ImageIsCurrent(surface, state, snapshot)
				|| RefuseImage(surface, state)) {
				return;
			}
			auto expiry = snapshot.expiry;
			if (snapshot.clockGeneration != state->clockGeneration) {
				expiry.deadline = EffectiveExpiry(state).deadline;
			}
			if (RefuseExpiring(surface, state, expiry)) {
				return;
			}
			state->image = image;
			state->imageSnapshot = snapshot;
			state->imageSnapshot.expiry = expiry;
			state->imageSnapshot.clockGeneration = state->clockGeneration;
			done(image, state->imageSnapshot);
		});
	});
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
	surface->colorsValue() | rpl::on_next([=](PeerQrColors colors) {
		pill->setColors(std::move(colors));
	}, pill->lifetime());

	const auto applyLink = [=](const QString &link) {
		state->link = link;
		surface->setLink(link, anim::type::normal);
	};
	const auto scheduleExpiry = [=] {
		free->expireTimer.callOnce(
			std::max(free->deadline - crl::now(), crl::time()),
			Qt::PreciseTimer);
	};
	const auto dropMenu = [=] {
		++free->menuGeneration;
		if (const auto menu = free->menu.get()) {
			menu->hideMenu(true);
		}
	};
	const auto dropPending = [=] {
		++free->generation;
		free->requesting = std::nullopt;
		dropMenu();
	};
	const auto applyFee = [=] {
		free->mode = FreeMode::Fee;
		free->expireTimer.cancel();
		pill->showFee(free->price);
		applyLink(state->plainLink);
	};
	const auto returnToFee = [=] {
		dropPending();
		applyFee();
	};
	const auto applyCode = [=](
			Request what,
			const Info::Profile::FreeCode &code) {
		const auto now = crl::now();
		const auto temporary = (what == Request::Temporary)
			|| (what == Request::ResetTemporary);
		if (code.link.isEmpty()
			|| (temporary && code.expires <= base::unixtime::now())) {
			if (what == Request::ResetTemporary
				|| what == Request::ResetPermanent) {
				returnToFee();
			}
			ShowFreeToast(surface, state, {
				.text = tr::lng_peer_qr_link_unavailable(tr::now, tr::marked),
			});
			return;
		}
		if (temporary) {
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
		const auto permanent = (what == Request::Permanent)
			|| (what == Request::ResetPermanent);
		if (free->requesting
			|| free->price <= 0
			|| (permanent && state->temporaryOnly.current())) {
			return;
		}
		dropMenu();
		free->requesting = what;
		const auto generation = ++free->generation;
		const auto current = [=] {
			if (free->generation != generation || !free->requesting) {
				return false;
			}
			free->requesting = std::nullopt;
			return !permanent || !state->temporaryOnly.current();
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
	const auto showMenu = [=] {
		const auto temporary = (free->mode == FreeMode::Temporary);
		const auto generation = free->menuGeneration;
		free->menu = base::make_unique_q<PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto menu = free->menu.get();
		menu->addAction(
			tr::lng_no_paid_token_menu_reset(tr::now),
			[=] {
				if (free->menuGeneration != generation) {
					return;
				}
				request(temporary
					? Request::ResetTemporary
					: Request::ResetPermanent);
			},
			&st::peerQrMenuReset);
		if (!state->temporaryOnly.current()) {
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
					if (free->menuGeneration != generation) {
						return;
					}
					request(temporary ? Request::Permanent : Request::Temporary);
				}));
		}
		menu->addAction(
			tr::lng_no_paid_token_menu_restore(tr::now),
			[=] {
				if (free->menuGeneration != generation) {
					return;
				}
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
		const auto geometry = pill->pillGeometry();
		const auto anchor = surface->mapToGlobal(QPoint(
			geometry.x() + (geometry.width() - menu->inner().width()) / 2,
			geometry.y()));
		// the shadow padding left of the inner panel is known only once prepared
		if (menu->prepareGeometryFor(anchor)
			&& menu->prepareGeometryFor(
				anchor - QPoint(menu->inner().x(), 0))) {
			menu->popupPrepared();
		}
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
	base::unixtime::updates() | rpl::on_next([=] {
		const auto now = crl::now();
		if (free->mode == FreeMode::Temporary) {
			free->deadline = DeadlineFor(free->expires, now);
			pill->setDeadline(free->deadline);
			scheduleExpiry();
		}
	}, box->lifetime());
	state->temporaryOnly.value() | rpl::on_next([=](bool only) {
		dropMenu();
		if (!only) {
			return;
		}
		if (free->requesting == Request::Permanent
			|| free->requesting == Request::ResetPermanent) {
			dropPending();
		}
		if (free->mode == FreeMode::Permanent) {
			applyFee();
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
		free->expireTimer.cancel();
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
}

[[nodiscard]] OpenLayout ComputeOpenLayout(
		QSize outer,
		QSize card,
		int panelHeight) {
	const auto margin = st::peerQrPanelMargin;
	const auto width = outer.width() - 2 * margin;
	const auto panel = QRect(
		margin,
		outer.height() - margin - panelHeight,
		width,
		panelHeight);
	const auto pillTop = panel.y()
		- st::peerQrPanelSkip
		- st::peerQrPillHeight;
	const auto bottom = pillTop - st::peerQrPillSkip;
	const auto edge = st::peerQrButtonSkip;
	const auto &close = st::peerQrClose;
	const auto scaleFor = [&](int top) {
		return std::clamp((bottom - top) / float64(card.height()), 0., 1.);
	};
	auto scale = scaleFor(edge);
	if ((outer.width() + card.width() * scale) / 2. + st::peerQrPanelSkip
		> outer.width() - edge - close.width) {
		scale = scaleFor(edge + close.height + st::peerQrPanelSkip);
	}
	return {
		.panel = panel,
		.card = QPointF(
			(outer.width() - card.width() * scale) / 2.,
			bottom - card.height() * scale),
		.scale = scale,
		.pillTop = pillTop,
	};
}

[[nodiscard]] const PeerQrSwatch *FindSwatch(
		const std::vector<PeerQrSwatch> &swatches,
		PeerQrThemeKey key) {
	const auto i = ranges::find(swatches, key, &PeerQrSwatch::key);
	return (i != end(swatches)) ? &*i : nullptr;
}

[[nodiscard]] PeerQrSwatch DefaultLookSwatch() {
	return { .appearance = PeerQrDefaultAppearance() };
}

[[nodiscard]] PeerQrSwatch BackdropHalf(
		const std::optional<PeerQrSwatch> &swatch) {
	if (!swatch || swatch->key.type == PeerQrThemeKey::Type::Gift) {
		return DefaultLookSwatch();
	}
	auto result = *swatch;
	result.key.emoji.clear();
	result.appearance.emoji.clear();
	return result;
}

[[nodiscard]] PeerQrSwatch WithEmoji(
		PeerQrSwatch backdrop,
		std::vector<QString> picks) {
	backdrop.key.emoji = picks;
	backdrop.appearance.emoji = std::move(picks);
	return backdrop;
}

[[nodiscard]] std::array<PeerQrPanel*, 3> Panels(
		not_null<const AppearanceState*> appearance) {
	return {
		appearance->colorsPanel,
		appearance->emojiPanel,
		appearance->giftsPanel,
	};
}

[[nodiscard]] PeerQrPanel *OpenPanel(
		not_null<const AppearanceState*> appearance) {
	for (const auto panel : Panels(appearance)) {
		if (panel && panel->toggled()) {
			return panel;
		}
	}
	return nullptr;
}

void RefreshControls(not_null<AppearanceState*> appearance) {
	Expects(appearance->island != nullptr);

	const auto island = appearance->island;
	const auto opacity = 1. - appearance->progress;
	const auto shown = (opacity > 0.);
	const auto blocked = appearance->panelShown;
	for (const auto &control : appearance->controls) {
		control->setOpacity(opacity);
		control->setVisible(shown);
		control->setAttribute(Qt::WA_TransparentForMouseEvents, blocked);
	}
	island->setVisible(shown && appearance->canSave);
	island->setAttribute(Qt::WA_TransparentForMouseEvents, blocked);
}

void SetEmojiPicks(
		not_null<AppearanceState*> appearance,
		std::vector<QString> picks) {
	appearance->emojiPicks = std::move(picks);
	if (appearance->picker) {
		appearance->picker->setPicks(appearance->emojiPicks);
	}
}

void ShowSwatch(
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		const PeerQrSwatch &swatch,
		anim::type animated) {
	appearance->current = swatch;
	surface->setAppearance(
		(swatch.appearance.empty()
			? PeerQrDefaultAppearance()
			: swatch.appearance),
		animated);
	const auto &key = swatch.key;
	auto selected = std::optional<PeerQrThemeKey>();
	auto selectedGift = std::optional<PeerQrThemeKey>();
	if (key.type == PeerQrThemeKey::Type::Gift) {
		selectedGift = PeerQrThemeKey{ .type = key.type, .gift = key.gift };
	} else {
		selected = PeerQrThemeKey{ .type = key.type, .id = key.id };
	}
	appearance->colors->setSelected(std::move(selected), animated);
	if (const auto gifts = appearance->gifts) {
		gifts->setSelected(std::move(selectedGift), animated);
	}
}

void ApplyServedTheme(
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		const PeerQrSwatch &swatch,
		anim::type animated) {
	SetEmojiPicks(appearance, swatch.key.emoji);
	ShowSwatch(surface, appearance, swatch, animated);
}

void ApplyEmojiPicks(
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance) {
	appearance->chosen = WithEmoji(
		BackdropHalf(appearance->current),
		appearance->emojiPicks);
	appearance->pending = std::nullopt;
	ShowSwatch(surface, appearance, *appearance->chosen, anim::type::normal);
}

void ChooseEmoji(
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		const QString &key) {
	auto &picks = appearance->emojiPicks;
	const auto i = ranges::find(picks, key);
	if (i != end(picks)) {
		picks.erase(i);
	} else {
		if (int(picks.size()) >= kMaxEmojiPicks) {
			picks.erase(picks.begin());
		}
		picks.push_back(key);
	}
	if (appearance->picker) {
		appearance->picker->setPicks(picks);
	}
	ApplyEmojiPicks(surface, appearance);
}

void ChooseGift(
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		const PeerQrSwatch &swatch) {
	SetEmojiPicks(appearance, {});
	appearance->chosen = swatch;
	appearance->pending = std::nullopt;
	ShowSwatch(surface, appearance, swatch, anim::type::normal);
}

void SaveAppearance(
		not_null<GenericBox*> box,
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		not_null<PeerData*> peer) {
	if (!appearance->chosen || !appearance->canSave) {
		return;
	}
	const auto key = appearance->chosen->key;
	const auto &served = appearance->served;
	const auto baseline = appearance->saving
		? appearance->saving
		: served
		? std::make_optional(served->key)
		: std::nullopt;
	if (key == baseline) {
		return;
	}
	const auto swatch = *appearance->chosen;
	const auto clear = [=] {
		if (appearance->saving == key) {
			appearance->saving = std::nullopt;
		}
	};
	const auto reset = crl::guard(box, clear);
	const auto show = box->uiShow();
	const auto send = [=](Fn<void()> done, Fn<void(QString)> fail) {
		if (key.type == PeerQrThemeKey::Type::Gift && appearance->saveGift) {
			appearance->saveGift(key, std::move(done), std::move(fail));
		} else {
			Info::Profile::SaveQrTheme(
				peer,
				key,
				std::move(done),
				std::move(fail));
		}
	};
	appearance->saving = key;
	// WHY: a pick only previews, so browsing the circles sends nothing;
	// the choice is saved once, when the panel or the box closes.
	send(crl::guard(box, [=] {
		clear();
		appearance->served = swatch;
		if (!appearance->chosen) {
			ApplyServedTheme(surface, appearance, swatch, anim::type::normal);
		}
	}), [=](QString error) {
		reset();
		if (!error.isEmpty()) {
			show->showToast(error);
		}
	});
}

void RevertToServed(
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance) {
	appearance->chosen = std::nullopt;
	if (const auto &served = appearance->served) {
		ApplyServedTheme(surface, appearance, *served, anim::type::normal);
	} else {
		SetEmojiPicks(appearance, {});
		appearance->current = std::nullopt;
		surface->setAppearance(PeerQrDefaultAppearance(), anim::type::normal);
		appearance->colors->setSelected(std::nullopt, anim::type::normal);
		if (const auto gifts = appearance->gifts) {
			gifts->setSelected(std::nullopt, anim::type::normal);
		}
	}
}

void SetupEmojiPanel(
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		not_null<Window::SessionController*> controller,
		not_null<PeerQrButton*> button,
		Fn<void(not_null<PeerQrPanel*>)> openPanel) {
	const auto container = CreateChild<PeerQrPanel>(surface.get());
	container->setRadius(st::emojiPanRadius);
	appearance->emojiPanel = container;
	surface->colorsValue() | rpl::on_next([=](const PeerQrColors &colors) {
		container->setColors(colors.control, colors.glass);
		if (appearance->picker) {
			appearance->picker->setColors(colors);
		}
	}, container->lifetime());
	container->progressValue() | rpl::filter([](float64 progress) {
		return (progress >= 1.);
	}) | rpl::on_next([=] {
		if (appearance->picker) {
			appearance->picker->setShown(true);
		}
	}, container->lifetime());

	const auto weak = base::make_weak(controller);
	button->setClickedCallback([=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		} else if (!appearance->picker) {
			auto picker = Info::Profile::CreateQrEmojiPicker(
				container,
				strong,
				appearance->emojiPicks);
			picker.setColors(surface->colors());
			container->setContent(picker.widget, PeerQrPanelContent::Masked);
			std::move(picker.chosen) | rpl::on_next([=](const QString &key) {
				ChooseEmoji(surface, appearance, key);
			}, picker.widget->lifetime());
			appearance->picker.emplace(std::move(picker));
		}
		appearance->picker->setPicks(appearance->emojiPicks);
		openPanel(container);
	});
}

[[nodiscard]] Fn<void()> SetupGiftList(
		not_null<GenericBox*> box,
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		not_null<PeerData*> peer,
		Window::SessionController *controller) {
	const auto gifts = appearance->gifts;
	auto list = Info::Profile::CreateQrGiftList(
		peer,
		controller,
		box->lifetime());
	std::move(
		list.slices
	) | rpl::on_next([=](Info::Profile::QrGiftsSlice &&slice) {
		gifts->setGifts(std::move(slice.gifts), slice.complete);
	}, box->lifetime());
	std::move(list.browseShown) | rpl::on_next([=](bool shown) {
		gifts->setBrowseShown(shown);
	}, box->lifetime());
	gifts->loadMoreRequests(
	) | rpl::on_next(std::move(list.loadMore), box->lifetime());
	gifts->browseRequests(
	) | rpl::on_next(std::move(list.browse), box->lifetime());
	gifts->picks() | rpl::on_next([=](const PeerQrSwatch &swatch) {
		ChooseGift(surface, appearance, swatch);
	}, box->lifetime());
	appearance->saveGift = std::move(list.save);
	return std::move(list.refresh);
}

void SetupGiftsPanel(
		not_null<GenericBox*> box,
		not_null<PeerQrSurface*> surface,
		not_null<AppearanceState*> appearance,
		not_null<PeerData*> peer,
		Window::SessionController *controller,
		not_null<PeerQrButton*> button,
		Fn<void(not_null<PeerQrPanel*>)> openPanel) {
	const auto container = CreateChild<PeerQrPanel>(surface.get());
	const auto gifts = CreateChild<PeerQrGiftsPanel>(container);
	container->setContent(gifts, PeerQrPanelContent::Masked);
	appearance->giftsPanel = container;
	appearance->gifts = gifts;

	surface->colorsValue() | rpl::on_next([=](const PeerQrColors &colors) {
		container->setColors(colors.control, colors.glass);
		gifts->setColors(colors);
	}, container->lifetime());
	Info::Profile::QrGiftSourceValue(
		&peer->session()
	) | rpl::on_next([=](PeerQrGiftSource &&source) {
		gifts->setSource(std::move(source));
	}, gifts->lifetime());

	const auto refresh = box->lifetime().make_state<Fn<void()>>();
	Info::Profile::QrThemeCanSaveValue(
		peer
	) | rpl::filter([](bool can) {
		return can;
	}) | rpl::take(1) | rpl::on_next([=] {
		*refresh = SetupGiftList(box, surface, appearance, peer, controller);
	}, box->lifetime());

	button->setClickedCallback([=] {
		if (*refresh) {
			(*refresh)();
		}
		openPanel(container);
		gifts->scrollToSelected();
	});
}

void SetupAppearance(
		not_null<GenericBox*> box,
		not_null<PeerQrSurface*> surface,
		not_null<BoxState*> state,
		not_null<PeerData*> peer,
		Window::SessionController *controller,
		not_null<PeerQrButton*> copy,
		PeerQrButton *share) {
	const auto appearance = &state->appearance;
	appearance->controls.push_back(copy);
	if (share) {
		appearance->controls.push_back(share);
	}
	const auto withEmoji = controller
		&& (&controller->session() == &peer->session());
	const auto island = AddIsland(surface, appearance, withEmoji ? 3 : 2);
	const auto colorsButton = AddIslandButton(
		island,
		appearance,
		surface,
		0,
		st::peerQrColors,
		tr::lng_qr_box_colors(tr::now));
	const auto container = CreateChild<PeerQrPanel>(surface.get());
	const auto colors = CreateChild<PeerQrColorsPanel>(container);
	container->setContent(colors, PeerQrPanelContent::Animated);
	appearance->island = island;
	appearance->colorsPanel = container;
	appearance->colors = colors;

	surface->colorsValue() | rpl::on_next([=](const PeerQrColors &value) {
		container->setColors(value.control, value.glass);
	}, container->lifetime());

	Info::Profile::QrThemeSwatchesValue(
		&peer->session()
	) | rpl::on_next([=](std::vector<PeerQrSwatch> &&swatches) {
		appearance->swatches = std::move(swatches);
		colors->setSwatches(appearance->swatches);
	}, box->lifetime());

	colors->picks() | rpl::on_next([=](PeerQrThemeKey key) {
		if (const auto swatch = FindSwatch(appearance->swatches, key)) {
			appearance->chosen = WithEmoji(*swatch, appearance->emojiPicks);
			appearance->pending = std::nullopt;
			ShowSwatch(
				surface,
				appearance,
				*appearance->chosen,
				anim::type::normal);
		}
	}, box->lifetime());

	const auto setPanelShown = [=](bool shown) {
		appearance->panelShown = shown;
		island->update();
		RefreshControls(appearance);
	};
	const auto openPanel = [=](not_null<PeerQrPanel*> panel) {
		if (appearance->panelShown) {
			return;
		}
		setPanelShown(true);
		box->setCloseByEscape(false);
		panel->toggle(true, anim::type::normal);
	};
	const auto closePanel = [=] {
		const auto panel = OpenPanel(appearance);
		if (!panel) {
			return;
		} else if (panel == appearance->emojiPanel && appearance->picker) {
			appearance->picker->setShown(false);
		}
		panel->toggle(false, anim::type::normal);
		SaveAppearance(box, surface, appearance, peer);
	};
	colorsButton->setClickedCallback([=] {
		openPanel(container);
		colors->scrollToSelected();
	});
	if (withEmoji) {
		SetupEmojiPanel(
			surface,
			appearance,
			controller,
			AddIslandButton(
				island,
				appearance,
				surface,
				1,
				st::peerQrEmoji,
				tr::lng_qr_box_emoji(tr::now)),
			openPanel);
	}
	SetupGiftsPanel(
		box,
		surface,
		appearance,
		peer,
		withEmoji ? controller : nullptr,
		AddIslandButton(
			island,
			appearance,
			surface,
			withEmoji ? 2 : 1,
			st::peerQrGift,
			tr::lng_qr_box_gifts(tr::now)),
		openPanel);

	// the controls fade above the panels' glass, never under it
	island->raise();
	copy->raise();
	if (share) {
		share->raise();
	}

	Info::Profile::QrServedThemeValue(
		peer
	) | rpl::on_next([=](Info::Profile::QrServedTheme &&theme) {
		appearance->served = theme.swatch;
		if (appearance->chosen) {
			return;
		} else if (theme.kept || appearance->shown) {
			ApplyServedTheme(
				surface,
				appearance,
				theme.swatch,
				theme.kept ? anim::type::instant : anim::type::normal);
		} else {
			appearance->pending = std::move(theme.swatch);
		}
	}, box->lifetime());

	Info::Profile::QrThemeCanSaveValue(peer) | rpl::on_next([=](bool can) {
		appearance->canSave = can;
		if (!can) {
			closePanel();
			if (appearance->chosen) {
				RevertToServed(surface, appearance);
			}
		}
		RefreshControls(appearance);
	}, box->lifetime());
	const auto emojiPanel = appearance->emojiPanel;
	const auto giftsPanel = appearance->giftsPanel;
	rpl::merge(
		surface->events() | rpl::filter([](not_null<QEvent*> e) {
			return (e->type() == QEvent::MouseButtonPress);
		}) | rpl::to_empty,
		box->events() | rpl::filter([](not_null<QEvent*> e) {
			return (e->type() == QEvent::KeyPress)
				&& (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Escape);
		}) | rpl::to_empty,
		container->outsidePresses(),
		(emojiPanel ? emojiPanel->outsidePresses() : rpl::never<>()),
		(giftsPanel ? giftsPanel->outsidePresses() : rpl::never<>())
	) | rpl::on_next(closePanel, box->lifetime());
	for (const auto panel : Panels(appearance)) {
		if (!panel) {
			continue;
		}
		panel->shownValue() | rpl::filter([](bool shown) {
			return !shown;
		}) | rpl::on_next([=] {
			// the Escape that closes the panel must not reach the layer as well
			PostponeCall(box, [=] {
				if (!OpenPanel(appearance)) {
					box->setCloseByEscape(true);
				}
			});
			setPanelShown(false);
		}, box->lifetime());
	}

	box->boxClosing() | rpl::on_next([=] {
		SaveAppearance(box, surface, appearance, peer);
	}, box->lifetime());
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
	const auto ownBox = peerBox && peer->isSelf();
	const auto state = box->lifetime().make_state<BoxState>();
	const auto surface = box->addRow(
		object_ptr<PeerQrSurface>(box, PeerQrDefaultAppearance()),
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
	const auto weakBox = base::make_weak(box);
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
	state->shareCutoffTimer.setCallback([=] {
		CloseShareBox(surface, state);
	});
	base::unixtime::updates() | rpl::on_next([=] {
		const auto now = crl::now();
		++state->clockGeneration;
		if (state->plainExpires) {
			state->plainDeadline = DeadlineFor(state->plainExpires, now);
		}
		auto &cached = state->imageSnapshot;
		if (cached.expiry.expires) {
			cached.expiry.deadline = DeadlineFor(cached.expiry.expires, now);
			cached.clockGeneration = state->clockGeneration;
		}
		if (state->shareExpiry.expires) {
			state->shareExpiry.deadline = DeadlineFor(
				state->shareExpiry.expires,
				now);
			ScheduleShareCutoff(surface, state);
		}
	}, box->lifetime());
	if (ownBox) {
		SetupFreeCodes(box, surface, state, peer);
	}

	if (peerBox) {
		Info::Profile::QrEmojiSourceValue(
			&peer->session()
		) | rpl::on_next([=](PeerQrEmojiSource &&source) {
			surface->setEmojiSource(std::move(source));
		}, box->lifetime());
		Info::Profile::QrGiftSourceValue(
			&peer->session()
		) | rpl::on_next([=](PeerQrGiftSource &&source) {
			surface->setGiftSource(std::move(source));
		}, box->lifetime());
		SetupAppearance(box, surface, state, peer, controller, copy, share);
		if (!ownBox) {
			Info::Profile::UsernameValue(
				peer,
				true
			) | rpl::on_next([=](const TextWithEntities &username) {
				state->username = username.text.toUpper();
				surface->setUsername(state->username);
			}, box->lifetime());
		}
	}
	if (ownBox) {
		Info::Profile::OwnQrLinkValue(&peer->session(), [=](QString error) {
			if (!error.isEmpty()) {
				ShowFreeToast(surface, state, { .text = tr::marked(error) });
			}
		}) | rpl::on_next([=](const Info::Profile::OwnQrLink &value) {
			if (state->plainExpires != value.expires) {
				state->plainDeadline = value.expires
					? DeadlineFor(value.expires, crl::now())
					: crl::time();
			}
			state->plainLink = value.link;
			state->kind = value.kind;
			state->plainExpires = value.expires;
			state->username = value.label;
			const auto contact = (value.kind
				== Info::Profile::OwnQrLinkKind::Contact);
			if (contact) {
				surface->setCountdown(value.label);
			} else {
				surface->setUsername(value.label);
			}
			if (state->free.mode == FreeMode::Fee) {
				state->link = value.link;
				surface->setLink(value.link, anim::type::normal);
			}
			state->temporaryOnly = contact;
		}, box->lifetime());
	} else {
		QrLinkValue(
			peer,
			customLink
		) | rpl::on_next([=](const QString &link) {
			state->plainLink = link;
			state->link = link;
			surface->setLink(link);
			if (link.isEmpty()) {
				box->showFinishes() | rpl::on_next([=] {
					box->closeBox();
				}, box->lifetime());
				box->closeBox();
			}
		}, box->lifetime());
	}

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
		auto pillTop = float64(bottom + st::peerQrPillSkip);
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
		const auto height = rowTop + copy->height() + skip;
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
		surface->resize(width, height);
		if (const auto island = state->appearance.island) {
			const auto &appearance = state->appearance;
			const auto outer = QSize(width, height);
			const auto panelWidth = width - 2 * st::peerQrPanelMargin;
			const auto table = ComputeOpenLayout(
				outer,
				card.size(),
				PeerQrColorsPanel::HeightForWidth(panelWidth));
			const auto gifts = appearance.gifts;
			const auto giftsOpen = gifts
				? ComputeOpenLayout(
					outer,
					card.size(),
					gifts->openHeight(panelWidth))
				: table;
			const auto &open = appearance.giftsActive ? giftsOpen : table;
			const auto progress = appearance.progress;
			const auto lerp = [&](float64 from, float64 till) {
				return from + (till - from) * progress;
			};
			island->moveToLeft((width - island->width()) / 2, rowTop, width);
			for (const auto panel : Panels(&appearance)) {
				if (panel) {
					panel->setGeometry((panel == appearance.giftsPanel)
						? giftsOpen.panel
						: table.panel);
					panel->setIslandRect(island->geometry());
				}
			}
			surface->setCardPlacement(
				QPointF(
					lerp(card.x(), open.card.x()),
					lerp(card.y(), open.card.y())),
				lerp(1., open.scale));
			pillTop = lerp(pillTop, open.pillTop);
		}
		if (const auto pill = state->free.pill) {
			pill->setPillPosition(width / 2, int(std::round(pillTop)));
		}
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
	if (const auto panel = state->appearance.colorsPanel) {
		const auto emoji = state->appearance.emojiPanel;
		const auto gifts = state->appearance.giftsPanel;
		rpl::combine(
			panel->progressValue(),
			emoji ? emoji->progressValue() : rpl::single(0.),
			gifts ? gifts->progressValue() : rpl::single(0.)
		) | rpl::on_next([=](
				float64 colorsShown,
				float64 emojiShown,
				float64 giftsShown) {
			auto &appearance = state->appearance;
			appearance.progress = std::max({
				colorsShown,
				emojiShown,
				giftsShown,
			});
			appearance.giftsActive = (giftsShown > 0.);
			RefreshControls(&appearance);
			relayout();
		}, box->lifetime());
	}
	if (const auto gifts = state->appearance.gifts) {
		gifts->openHeightChanges() | rpl::on_next(relayout, box->lifetime());
	}

	close->setClickedCallback([=] {
		box->closeBox();
	});
	const auto dropRendered = [=](const ImageSnapshot &snapshot) {
		return !ImageIsCurrent(surface, state, snapshot)
			|| RefuseImage(surface, state)
			|| RefuseExpiring(surface, state, snapshot.expiry);
	};
	copy->setClickedCallback([=] {
		if (RefuseImage(surface, state)) {
			return;
		}
		RenderCardImage(box, surface, state, copy, [=](
				QImage image,
				const ImageSnapshot &snapshot) {
			if (dropRendered(snapshot)) {
				return;
			}
			auto mime = std::make_unique<QMimeData>();
			mime->setImageData(std::move(image));
			const auto show = box->uiShow();
			QGuiApplication::clipboard()->setMimeData(mime.release());
			show->showToast({
				.text = { tr::lng_group_invite_qr_copied(tr::now) },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		});
	});
	if (share) {
		share->setClickedCallback([=] {
			if (RefuseImage(surface, state)) {
				return;
			}
			RenderCardImage(box, surface, state, share, [=](
						QImage image,
						const ImageSnapshot &snapshot) {
				if (dropRendered(snapshot)) {
					return;
				}
				if (const auto strong = weakController.get()) {
					const auto expiry = snapshot.expiry;
					const auto link = snapshot.link;
					const auto shared = FastShareImage(
						strong->uiShow(),
						std::move(image),
						link);
					if (weakBox) {
						TrackShareBox(surface, state, shared, expiry);
					} else if (expiry.expires) {
						if (const auto orphaned = shared.get()) {
							orphaned->closeBox();
						}
					}
				}
			});
		});
	}
	box->lifetime().add([=] {
		// the cut-off timer dies with the box, so must the share box it guards
		state->shareCutoffTimer.cancel();
		state->free.expireTimer.cancel();
		if (state->shareExpiry.expires) {
			if (const auto strong = base::take(state->shareBox).get()) {
				strong->closeBox();
			}
		}
	});

	box->showFinishes(
	) | rpl::take(1) | rpl::on_next([=] {
		surface->startAnimations();
		auto &appearance = state->appearance;
		appearance.shown = true;
		if (const auto pending = base::take(appearance.pending)) {
			ApplyServedTheme(surface, &appearance, *pending, anim::type::normal);
		}
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
	if (peer && (peer->isSelf() || !peer->username().isEmpty())) {
		show->show(Box(
			Ui::FillPeerQrBox,
			peer,
			std::nullopt,
			nullptr,
			nullptr));
	}
}

} // namespace Ui

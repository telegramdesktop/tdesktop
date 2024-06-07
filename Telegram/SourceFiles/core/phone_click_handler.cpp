/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/phone_click_handler.h"

#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mtproto/sender.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_calls.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator.
#include "styles/style_menu_icons.h"

namespace {

[[nodiscard]] QString Trim(QString text) {
	return text
		.replace('+', QString())
		.replace(' ', QString())
		.replace('-', QString());
}

class ResolvePhoneAction final : public Ui::Menu::ItemBase {
public:
	ResolvePhoneAction(
		not_null<Ui::RpWidget*> parent,
		const style::Menu &st,
		const QString &phone,
		not_null<Window::SessionController*> controller);

	bool isEnabled() const override;
	not_null<QAction*> action() const override;

	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	int contentHeight() const override;

private:
	void prepare();
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	rpl::variable<PeerData*> _peer;
	rpl::variable<bool> _loaded;
	Ui::PeerUserpicView _userpicView;

	MTP::Sender _api;

	Ui::Text::String _above;
	Ui::Text::String _below;
	int _aboveWidth = 0;
	int _belowWidth = 0;
	const int _height = 0;

};

ResolvePhoneAction::ResolvePhoneAction(
	not_null<Ui::RpWidget*> parent,
	const style::Menu &st,
	const QString &phone,
	not_null<Window::SessionController*> controller)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _api(&controller->session().mtp())
, _height(rect::m::sum::v(st::groupCallJoinAsPadding)
	+ st::groupCallJoinAsPhotoSize) {
	setAcceptBoth(true);
	initResizeHook(parent->sizeValue());
	setClickedCallback([=] {
		if (const auto peer = _peer.current()) {
			controller->showPeerInfo(peer);
		}
	});

	const auto formattedPhone = Trim(phone);

	const auto owner = &controller->session().data();

	if (const auto peer = owner->userByPhone(formattedPhone)) {
		_peer = peer;
		_loaded.force_assign(true);
	} else {
		_api.request(MTPcontacts_ResolvePhone(
			MTP_string(phone)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			result.match([&](const MTPDcontacts_resolvedPeer &data) {
				owner->processUsers(data.vusers());
				owner->processChats(data.vchats());
				if (const auto peerId = peerFromMTP(data.vpeer())) {
					_peer = owner->peer(peerId);
				}
				_loaded.force_assign(true);
			});
		}).fail([=](const MTP::Error &error) {
			if (error.code() == 400) {
				_peer.force_assign(nullptr);
				_loaded.force_assign(true);
			}
		}).send();
	}

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);
		paint(p);
	}, lifetime());

	enableMouseSelecting();
	prepare();
}

void ResolvePhoneAction::paint(Painter &p) {
	const auto selected = isSelected() && _peer.current();
	const auto height = contentHeight();
	if (selected && _st.itemBgOver->c.alpha() < 255) {
		p.fillRect(0, 0, width(), height, _st.itemBg);
	}
	p.fillRect(0, 0, width(), height, selected ? _st.itemBgOver : _st.itemBg);
	if (isEnabled()) {
		paintRipple(p, 0, 0);
	}

	const auto &padding = st::groupCallJoinAsPadding;
	const auto textLeft = padding.left()
		+ st::groupCallJoinAsPhotoSize
		+ padding.left();
	if (const auto peer = _peer.current()) {
		peer->paintUserpic(
			p,
			_userpicView,
			padding.left(),
			padding.top(),
			st::groupCallJoinAsPhotoSize);
		p.setPen(selected ? _st.itemFgOver : _st.itemFg);
		_above.drawLeftElided(
			p,
			textLeft,
			st::groupCallJoinAsTextTop,
			width() - textLeft - padding.right(),
			width());
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		_below.drawLeftElided(
			p,
			textLeft,
			st::groupCallJoinAsNameTop,
			_belowWidth,
			width());
	} else {
		p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
		const auto w = width() - padding.left() - padding.right();
		_below.draw(p, Ui::Text::PaintContext{
			.position = QPoint(
				(width() - w) / 2,
				(height - _below.countHeight(w)) / 2),
			.outerWidth = w,
			.availableWidth = w,
			.align = style::al_center,
			.elisionLines = 2,
		});
	}
}

void ResolvePhoneAction::prepare() {
	rpl::combine(
		tr::lng_context_view_profile(),
		_peer.value(
		) | rpl::map([](PeerData *peer) {
			return peer
				? Info::Profile::NameValue(peer)
				: rpl::single(QString());
		}) | rpl::flatten_latest(),
		tr::lng_menu_not_contact(),
		_loaded.value(
		) | rpl::map([](bool loaded) {
			return loaded
				? rpl::single(QString())
				: tr::lng_contacts_loading();
		}) | rpl::flatten_latest()
	) | rpl::start_with_next([=](
			QString text,
			QString name,
			QString no,
			QString loading) {
		const auto &padding = st::groupCallJoinAsPadding;
		QWidget::setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!_peer.current());
		const auto above = name;
		const auto below = !loading.isEmpty()
			? loading
			: name.isEmpty()
			? no
			: text;
		const auto options = kDefaultTextOptions;
		const auto tempWidth = [&] {
			_below.setMarkedText(_st.itemStyle, { text }, options);
			return _below.maxWidth();
		}();
		const auto textLeft = padding.left()
			+ st::groupCallJoinAsPhotoSize
			+ padding.left();
		const auto w = std::clamp(
			(textLeft + tempWidth + padding.right()),
			_st.widthMin,
			_st.widthMax);
		if (!no.isEmpty()) {
			_below = Ui::Text::String(w);
		}
		_above.setMarkedText(_st.itemStyle, { above }, options);
		_below.setMarkedText(_st.itemStyle, { below }, options);
		setMinWidth(w);
		_aboveWidth = w - textLeft - padding.right();
		_belowWidth = w
			- ((loading.isEmpty() && name.isEmpty()) ? 0 : textLeft)
			- padding.right();
		update();
	}, lifetime());
}

bool ResolvePhoneAction::isEnabled() const {
	return true;
}

not_null<QAction*> ResolvePhoneAction::action() const {
	return _dummyAction;
}

QPoint ResolvePhoneAction::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage ResolvePhoneAction::prepareRippleMask() const {
	return Ui::RippleAnimation::RectMask(size());
}

int ResolvePhoneAction::contentHeight() const {
	return _height;
}

void ResolvePhoneAction::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected() || !_peer.current()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

} // namespace

PhoneClickHandler::PhoneClickHandler(
	not_null<Main::Session*> session,
	QString text)
: _session(session)
, _text(text) {
}

void PhoneClickHandler::onClick(ClickContext context) const {
	if (context.button != Qt::LeftButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	const auto controller = my.sessionWindow.get();
	const auto pos = QCursor::pos();
	if (!controller) {
		return;
	}
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		controller->content(),
		st::popupMenuWithIcons);

	const auto phone = _text;

#if 0
	const auto maybeContact = [&]() -> PeerData* {
		const auto &chats = controller->session().data().contactsList();
		for (const auto &row : chats->all()) {
			if (const auto history = row->history()) {
				if (const auto user = history->peer->asUser()) {
					if (Trim(user->phone()) == Trim(phone)) {
						return user;
					}
				}
			}
		}
		return nullptr;
	}();
#endif

	menu->addAction(tr::lng_profile_copy_phone(tr::now), [=] {
		TextUtilities::SetClipboardText(
			TextForMimeData::Simple(phone.trimmed()));
	}, &st::menuIconCopy);

	menu->addSeparator(&st::popupMenuExpandedSeparator.menu.separator);

	menu->addAction(
		base::make_unique_q<ResolvePhoneAction>(
			menu,
			menu->st().menu,
			phone,
			controller));

	menu->popup(pos);
}

auto PhoneClickHandler::getTextEntity() const -> TextEntity {
	return { EntityType::Phone };
}

QString PhoneClickHandler::tooltip() const {
	return _text;
}

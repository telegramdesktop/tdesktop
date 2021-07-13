/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_contact.h"

#include "lang/lang_keys.h"
#include "layout.h"
#include "mainwindow.h"
#include "boxes/add_contact_box.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "window/window_session_controller.h"
#include "ui/empty_userpic.h"
#include "ui/text/text_options.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_media_types.h"
#include "data/data_cloud_file.h"
#include "main/main_session.h"
#include "app.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

ClickHandlerPtr sendMessageClickHandler(PeerData *peer) {
	return std::make_shared<LambdaClickHandler>([peer] {
		App::wnd()->sessionController()->showPeerHistory(
			peer->id,
			Window::SectionShow::Way::Forward);
	});
}

ClickHandlerPtr addContactClickHandler(not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto fullId = item->fullId();
	return std::make_shared<LambdaClickHandler>([=] {
		if (const auto item = session->data().message(fullId)) {
			if (const auto media = item->media()) {
				if (const auto contact = media->sharedContact()) {
					Ui::show(Box<AddContactBox>(
						session,
						contact->firstName,
						contact->lastName,
						contact->phoneNumber));
				}
			}
		}
	});
}

} // namespace

Contact::Contact(
	not_null<Element*> parent,
	UserId userId,
	const QString &first,
	const QString &last,
	const QString &phone)
: Media(parent)
, _userId(userId)
, _fname(first)
, _lname(last)
, _phone(App::formatPhone(phone)) {
	history()->owner().registerContactView(userId, parent);

	_name.setText(
		st::semiboldTextStyle,
		tr::lng_full_name(tr::now, lt_first_name, first, lt_last_name, last).trimmed(),
		Ui::NameTextOptions());
	_phonew = st::normalFont->width(_phone);
}

Contact::~Contact() {
	history()->owner().unregisterContactView(_userId, _parent);
	if (_userpic) {
		_userpic = nullptr;
		_parent->checkHeavyPart();
	}
}

void Contact::updateSharedContactUserId(UserId userId) {
	if (_userId != userId) {
		history()->owner().unregisterContactView(_userId, _parent);
		_userId = userId;
		history()->owner().registerContactView(_userId, _parent);
	}
}

QSize Contact::countOptimalSize() {
	const auto item = _parent->data();
	auto maxWidth = st::msgFileMinWidth;

	_contact = _userId
		? item->history()->owner().userLoaded(_userId)
		: nullptr;
	if (_contact) {
		_contact->loadUserpic();
	} else {
		const auto full = _name.toString();
		_photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Data::PeerUserpicColor(_userId
				? peerFromUser(_userId)
				: Data::FakePeerIdForJustName(full)),
			full);
	}
	if (_contact && _contact->isContact()) {
		_linkl = sendMessageClickHandler(_contact);
		_link = tr::lng_profile_send_message(tr::now).toUpper();
	} else if (_userId) {
		_linkl = addContactClickHandler(_parent->data());
		_link = tr::lng_profile_add_contact(tr::now).toUpper();
	}
	_linkw = _link.isEmpty() ? 0 : st::semiboldFont->width(_link);

	const auto &st = _userId ? st::msgFileThumbLayout : st::msgFileLayout;

	const auto tleft = st.padding.left() + st.thumbSize + st.padding.right();
	const auto tright = st.padding.left();
	if (_userId) {
		accumulate_max(maxWidth, tleft + _phonew + tright);
	} else {
		accumulate_max(maxWidth, tleft + _phonew + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	accumulate_max(maxWidth, tleft + _name.maxWidth() + tright);
	accumulate_min(maxWidth, st::msgMaxWidth);
	auto minHeight = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (_userId) {
		const auto msgsigned = item->Get<HistoryMessageSigned>();
		const auto views = item->Get<HistoryMessageViews>();
		if ((msgsigned && !msgsigned->isAnonymousRank)
			|| (views
				&& (views->views.count >= 0 || views->replies.count > 0))) {
			minHeight += st::msgDateFont->height - st::msgDateDelta.y();
		}
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

void Contact::draw(Painter &p, const QRect &r, TextSelection selection, crl::time ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	accumulate_min(paintw, maxWidth());

	const auto &st = _userId ? st::msgFileThumbLayout : st::msgFileLayout;
	const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto nameleft = st.padding.left() + st.thumbSize + st.padding.right();
	const auto nametop = st.nameTop - topMinus;
	const auto nameright = st.padding.left();
	const auto statustop = st.statusTop - topMinus;
	const auto linktop = st.linkTop - topMinus;
	if (_userId) {
		QRect rthumb(style::rtlrect(st.padding.left(), st.padding.top() - topMinus, st.thumbSize, st.thumbSize, paintw));
		if (_contact) {
			const auto was = (_userpic != nullptr);
			_contact->paintUserpic(p, _userpic, rthumb.x(), rthumb.y(), st.thumbSize);
			if (!was && _userpic) {
				history()->owner().registerHeavyViewPart(_parent);
			}
		} else {
			_photoEmpty->paint(p, st.padding.left(), st.padding.top() - topMinus, paintw, st.thumbSize);
		}
		if (selected) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(p.textPalette().selectOverlay);
			p.setPen(Qt::NoPen);
			p.drawEllipse(rthumb);
		}

		bool over = ClickHandler::showAsActive(_linkl);
		p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
		p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
		p.drawTextLeft(nameleft, linktop, paintw, _link, _linkw);
	} else {
		_photoEmpty->paint(p, st.padding.left(), st.padding.top() - topMinus, paintw, st.thumbSize);
	}
	const auto namewidth = paintw - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
	_name.drawLeftElided(p, nameleft, nametop, namewidth, paintw);

	auto &status = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, paintw, _phone);
}

TextState Contact::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (_userId) {
		const auto &st = _userId ? st::msgFileThumbLayout : st::msgFileLayout;
		const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
		const auto nameleft = st.padding.left() + st.thumbSize + st.padding.right();
		const auto linktop = st.linkTop - topMinus;
		if (style::rtlrect(nameleft, linktop, _linkw, st::semiboldFont->height, width()).contains(point)) {
			result.link = _linkl;
			return result;
		}
	}
	if (QRect(0, 0, width(), height()).contains(point) && _contact) {
		result.link = _contact->openLink();
		return result;
	}
	return result;
}

void Contact::unloadHeavyPart() {
	_userpic = nullptr;
}

bool Contact::hasHeavyPart() const {
	return (_userpic != nullptr);
}

} // namespace HistoryView

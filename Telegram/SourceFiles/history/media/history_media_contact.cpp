/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/media/history_media_contact.h"

#include "lang/lang_keys.h"
#include "layout.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "boxes/add_contact_box.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "window/window_controller.h"
#include "ui/empty_userpic.h"
#include "ui/text_options.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "styles/style_history.h"

namespace {

using TextState = HistoryView::TextState;

} // namespace

namespace {

ClickHandlerPtr sendMessageClickHandler(PeerData *peer) {
	return std::make_shared<LambdaClickHandler>([peer] {
		App::wnd()->controller()->showPeerHistory(
			peer->id,
			Window::SectionShow::Way::Forward);
	});
}

ClickHandlerPtr addContactClickHandler(not_null<HistoryItem*> item) {
	return std::make_shared<LambdaClickHandler>([fullId = item->fullId()] {
		if (const auto item = App::histItemById(fullId)) {
			if (const auto media = item->media()) {
				if (const auto contact = media->sharedContact()) {
					Ui::show(Box<AddContactBox>(
						contact->firstName,
						contact->lastName,
						contact->phoneNumber));
				}
			}
		}
	});
}

} // namespace

HistoryContact::HistoryContact(
	not_null<Element*> parent,
	UserId userId,
	const QString &first,
	const QString &last,
	const QString &phone)
: HistoryMedia(parent)
, _userId(userId)
, _fname(first)
, _lname(last)
, _phone(App::formatPhone(phone)) {
	Auth().data().registerContactView(userId, parent);

	_name.setText(
		st::semiboldTextStyle,
		lng_full_name(lt_first_name, first, lt_last_name, last).trimmed(),
		Ui::NameTextOptions());
	_phonew = st::normalFont->width(_phone);
}

HistoryContact::~HistoryContact() {
	Auth().data().unregisterContactView(_userId, _parent);
}

void HistoryContact::updateSharedContactUserId(UserId userId) {
	if (_userId != userId) {
		Auth().data().unregisterContactView(_userId, _parent);
		_userId = userId;
		Auth().data().registerContactView(_userId, _parent);
	}
}

QSize HistoryContact::countOptimalSize() {
	const auto item = _parent->data();
	auto maxWidth = st::msgFileMinWidth;

	_contact = _userId ? App::userLoaded(_userId) : nullptr;
	if (_contact) {
		_contact->loadUserpic();
	} else {
		_photoEmpty = std::make_unique<Ui::EmptyUserpic>(
			Data::PeerUserpicColor(_userId ? _userId : _parent->data()->id),
			_name.originalText());
	}
	if (_contact
		&& _contact->contactStatus() == UserData::ContactStatus::Contact) {
		_linkl = sendMessageClickHandler(_contact);
		_link = lang(lng_profile_send_message).toUpper();
	} else if (_userId) {
		_linkl = addContactClickHandler(_parent->data());
		_link = lang(lng_profile_add_contact).toUpper();
	}
	_linkw = _link.isEmpty() ? 0 : st::semiboldFont->width(_link);

	auto tleft = 0;
	auto tright = 0;
	if (_userId) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		accumulate_max(maxWidth, tleft + _phonew + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		accumulate_max(maxWidth, tleft + _phonew + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	accumulate_max(maxWidth, tleft + _name.maxWidth() + tright);
	accumulate_min(maxWidth, st::msgMaxWidth);
	auto minHeight = 0;
	if (_userId) {
		minHeight = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
		if (item->Has<HistoryMessageSigned>()
			|| item->Has<HistoryMessageViews>()) {
			minHeight += st::msgDateFont->height - st::msgDateDelta.y();
		}
	} else {
		minHeight = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

void HistoryContact::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintx = 0, painty = 0, paintw = width(), painth = height();

	auto outbg = _parent->hasOutLayout();
	bool selected = (selection == FullSelection);

	accumulate_min(paintw, maxWidth());

	auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop - topMinus;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop - topMinus;
		linktop = st::msgFileThumbLinkTop - topMinus;

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, st::msgFileThumbSize, st::msgFileThumbSize, paintw));
		if (_contact) {
			_contact->paintUserpic(p, rthumb.x(), rthumb.y(), st::msgFileThumbSize);
		} else {
			_photoEmpty->paint(p, st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, paintw, st::msgFileThumbSize);
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
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop - topMinus;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop - topMinus;

		_photoEmpty->paint(p, st::msgFilePadding.left(), st::msgFilePadding.top() - topMinus, paintw, st::msgFileSize);
	}
	auto namewidth = paintw - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
	_name.drawLeftElided(p, nameleft, nametop, namewidth, paintw);

	auto &status = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, paintw, _phone);
}

TextState HistoryContact::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		linktop = st::msgFileThumbLinkTop - topMinus;
		if (rtlrect(nameleft, linktop, _linkw, st::semiboldFont->height, width()).contains(point)) {
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

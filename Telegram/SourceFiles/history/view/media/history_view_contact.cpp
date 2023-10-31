/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_contact.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "lang/lang_keys.h"
#include "layout/layout_selection.h"
#include "mainwindow.h"
#include "boxes/add_contact_box.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "window/window_session_controller.h"
#include "ui/empty_userpic.h"
#include "ui/chat/chat_style.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_media_types.h"
#include "data/data_cloud_file.h"
#include "main/main_session.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

ClickHandlerPtr SendMessageClickHandler(PeerData *peer) {
	return std::make_shared<LambdaClickHandler>([peer](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (controller->session().uniqueId()
					!= peer->session().uniqueId()) {
				return;
			}
			controller->showPeerHistory(
				peer->id,
				Window::SectionShow::Way::Forward);
		}
	});
}

ClickHandlerPtr AddContactClickHandler(not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto fullId = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			if (controller->session().uniqueId() != session->uniqueId()) {
				return;
			}
			if (const auto item = session->data().message(fullId)) {
				if (const auto media = item->media()) {
					if (const auto contact = media->sharedContact()) {
						controller->show(Box<AddContactBox>(
							session,
							contact->firstName,
							contact->lastName,
							contact->phoneNumber));
					}
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
, _phone(Ui::FormatPhone(phone)) {
	history()->owner().registerContactView(userId, parent);

	_name.setText(
		st::semiboldTextStyle,
		tr::lng_full_name(tr::now, lt_first_name, first, lt_last_name, last).trimmed(),
		Ui::NameTextOptions());
	_phonew = st::normalFont->width(_phone);
}

Contact::~Contact() {
	history()->owner().unregisterContactView(_userId, _parent);
	if (!_userpic.null()) {
		_userpic = {};
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
			Ui::EmptyUserpic::UserpicColor(Data::DecideColorIndex(_userId
				? peerFromUser(_userId)
				: Data::FakePeerIdForJustName(full))),
			full);
	}
	if (_contact && _contact->isContact()) {
		_linkl = SendMessageClickHandler(_contact);
		_link = tr::lng_profile_send_message(tr::now).toUpper();
	} else if (_userId) {
		_linkl = AddContactClickHandler(_parent->data());
		_link = tr::lng_profile_add_contact(tr::now).toUpper();
	}
	_linkw = _link.isEmpty() ? 0 : st::semiboldFont->width(_link);

	const auto &st = _userId ? st::msgFileThumbLayout : st::msgFileLayout;

	const auto tleft = st.padding.left() + st.thumbSize + st.thumbSkip;
	const auto tright = st.padding.right();
	if (_userId) {
		accumulate_max(maxWidth, tleft + _phonew + tright);
	} else {
		accumulate_max(maxWidth, tleft + _phonew + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	accumulate_max(maxWidth, tleft + _name.maxWidth() + tright);
	accumulate_min(maxWidth, st::msgMaxWidth);
	auto minHeight = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (_parent->bottomInfoIsWide()) {
		minHeight += st::msgDateFont->height - st::msgDateDelta.y();
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

void Contact::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	const auto stm = context.messageStyle();

	accumulate_min(paintw, maxWidth());

	const auto &st = _userId ? st::msgFileThumbLayout : st::msgFileLayout;
	const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	const auto nameleft = st.padding.left() + st.thumbSize + st.thumbSkip;
	const auto nametop = st.nameTop - topMinus;
	const auto nameright = st.padding.right();
	const auto statustop = st.statusTop - topMinus;
	const auto linkshift = st::msgDateFont->height / 2;
	const auto linktop = st.linkTop - topMinus - linkshift;
	if (_userId) {
		QRect rthumb(style::rtlrect(st.padding.left(), st.padding.top() - topMinus, st.thumbSize, st.thumbSize, paintw));
		if (_contact) {
			const auto was = !_userpic.null();
			_contact->paintUserpic(p, _userpic, rthumb.x(), rthumb.y(), st.thumbSize);
			if (!was && !_userpic.null()) {
				history()->owner().registerHeavyViewPart(_parent);
			}
		} else {
			_photoEmpty->paintCircle(p, st.padding.left(), st.padding.top() - topMinus, paintw, st.thumbSize);
		}
		if (context.selected()) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(p.textPalette().selectOverlay);
			p.setPen(Qt::NoPen);
			p.drawEllipse(rthumb);
		}

		bool over = ClickHandler::showAsActive(_linkl);
		p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
		p.setPen(stm->msgFileThumbLinkFg);
		p.drawTextLeft(nameleft, linktop, paintw, _link, _linkw);
	} else {
		_photoEmpty->paintCircle(p, st.padding.left(), st.padding.top() - topMinus, paintw, st.thumbSize);
	}
	const auto namewidth = paintw - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(stm->historyFileNameFg);
	_name.drawLeftElided(p, nameleft, nametop, namewidth, paintw);

	p.setFont(st::normalFont);
	p.setPen(stm->mediaFg);
	p.drawTextLeft(nameleft, statustop, paintw, _phone);
}

TextState Contact::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);

	if (_userId) {
		const auto &st = _userId ? st::msgFileThumbLayout : st::msgFileLayout;
		const auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
		const auto nameleft = st.padding.left() + st.thumbSize + st.thumbSkip;
		const auto linkshift = st::msgDateFont->height / 2;
		const auto linktop = st.linkTop - topMinus - linkshift;
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
	_userpic = {};
}

bool Contact::hasHeavyPart() const {
	return !_userpic.null();
}

} // namespace HistoryView

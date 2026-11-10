/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_send_data.h"

#include "api/api_text_entities.h"
#include "data/data_document.h"
#include "inline_bots/inline_bot_result.h"
#include "iv/iv_rich_page.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_channel.h"
#include "ui/text/format_values.h" // Ui::FormatPhone

namespace InlineBots {
namespace internal {

QString SendData::getLayoutTitle(const Result *owner) const {
	return owner->_title;
}

QString SendData::getLayoutDescription(const Result *owner) const {
	return owner->_description;
}

not_null<HistoryItem*> SendDataCommon::makeMessage(
		const Result *owner,
		not_null<History*> history,
		HistoryItemCommonFields &&fields) const {
	auto distinct = getSentMessageFields();
	if (fields.replyTo) {
		fields.flags |= MessageFlag::HasReplyInfo;
	}
	return history->makeMessage(
		std::move(fields),
		std::move(distinct.text),
		std::move(distinct.media));
}

Data::SendError SendDataCommon::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = ChatRestriction::SendOther;
	return Data::RestrictionError(history->peer, type);
}

SendDataCommon::SentMessageFields SendText::getSentMessageFields() const {
	return { .text = { _message, _entities } };
}

SendRichMessage::SendRichMessage(
	not_null<Main::Session*> session,
	const MTPRichMessage &message)
: SendData(session)
, _page(Iv::ParseRichPage(session, message))
, _summary(Iv::FlattenRichPageSummary(_page)) {
}

bool SendRichMessage::isValid() const {
	return _page && !_page->blocks.empty();
}

not_null<HistoryItem*> SendRichMessage::makeMessage(
		const Result *owner,
		not_null<History*> history,
		HistoryItemCommonFields &&fields) const {
	if (fields.replyTo) {
		fields.flags |= MessageFlag::HasReplyInfo;
	}
	const auto item = history->makeMessage(
		std::move(fields),
		_summary,
		MTP_messageMediaEmpty());
	item->setRichPage(_page);
	return item;
}

Data::SendError SendRichMessage::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = ChatRestriction::SendOther;
	return Data::RestrictionError(history->peer, type);
}

QString SendRichMessage::getLayoutDescription(const Result *owner) const {
	auto result = SendData::getLayoutDescription(owner);
	if (result.isEmpty()) {
		return _summary.text;
	}
	return result;
}

SendDataCommon::SentMessageFields SendGeo::getSentMessageFields() const {
	if (_period) {
		using Flag = MTPDmessageMediaGeoLive::Flag;
		return { .media = MTP_messageMediaGeoLive(
			MTP_flags((_heading ? Flag::f_heading : Flag(0))
				| (_proximityNotificationRadius
					? Flag::f_proximity_notification_radius
					: Flag(0))),
			_location->toMTP(),
			MTP_int(_heading.value_or(0)),
			MTP_int(*_period),
			MTP_int(_proximityNotificationRadius.value_or(0))) };
	}
	return { .media = MTP_messageMediaGeo(_location->toMTP()) };
}

SendDataCommon::SentMessageFields SendVenue::getSentMessageFields() const {
	return { .media = MTP_messageMediaVenue(
		_location->toMTP(),
		MTP_string(_title),
		MTP_string(_address),
		MTP_string(_provider),
		MTP_string(_venueId),
		MTP_string(QString())) }; // venue_type
}

SendDataCommon::SentMessageFields SendContact::getSentMessageFields() const {
	return { .media = MTP_messageMediaContact(
		MTP_string(_phoneNumber),
		MTP_string(_firstName),
		MTP_string(_lastName),
		MTP_string(), // vcard
		MTP_long(0)) }; // user_id
}

QString SendContact::getLayoutDescription(const Result *owner) const {
	auto result = SendData::getLayoutDescription(owner);
	if (result.isEmpty()) {
		return Ui::FormatPhone(_phoneNumber);
	}
	return result;
}

not_null<HistoryItem*> SendPhoto::makeMessage(
		const Result *owner,
		not_null<History*> history,
		HistoryItemCommonFields &&fields) const {
	return history->makeMessage(
		std::move(fields),
		_photo,
		TextWithEntities{ _message, _entities });
}

Data::SendError SendPhoto::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = ChatRestriction::SendPhotos;
	return Data::RestrictionError(history->peer, type);
}

not_null<HistoryItem*> SendFile::makeMessage(
		const Result *owner,
		not_null<History*> history,
		HistoryItemCommonFields &&fields) const {
	return history->makeMessage(
		std::move(fields),
		_document,
		TextWithEntities{ _message, _entities });
}

Data::SendError SendFile::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = _document->requiredSendRight();
	return Data::RestrictionError(history->peer, type);
}

not_null<HistoryItem*> SendGame::makeMessage(
		const Result *owner,
		not_null<History*> history,
		HistoryItemCommonFields &&fields) const {
	return history->makeMessage(std::move(fields), _game);
}

Data::SendError SendGame::getErrorOnSend(
		const Result *owner,
		not_null<History*> history) const {
	const auto type = ChatRestriction::SendGames;
	return Data::RestrictionError(history->peer, type);
}

SendDataCommon::SentMessageFields SendInvoice::getSentMessageFields() const {
	return { .media = _media };
}

QString SendInvoice::getLayoutDescription(const Result *owner) const {
	return qs(_media.c_messageMediaInvoice().vdescription());
}

SendWidget::SendWidget(
	not_null<Main::Session*> session,
	const MTPDbotInlineMessageWidget &data)
: SendDataCommon(session)
, _caption{
	qs(data.vmessage()),
	Api::EntitiesFromMTP(session, data.ventities().value_or_empty()),
}
, _invertMedia(data.is_invert_media()) {
	using Flag = MTPDmessageMediaWidget::Flag;
	_media = MTP_messageMediaWidget(
		MTP_flags((data.vpublic_data() ? Flag::f_public_data : Flag(0))
			| (data.vchat_data() ? Flag::f_chat_data : Flag(0))
			| (data.vwidget_id() ? Flag::f_widget_id : Flag(0))),
		data.vwidth(),
		data.vheight(),
		data.vpayload(),
		MTP_bytes(data.vpublic_data().value_or_empty()),
		MTP_long(data.vpublic_data_hash().value_or_empty()),
		MTP_bytes(data.vchat_data().value_or_empty()),
		MTP_long(data.vwidget_id().value_or_empty()));
}

bool SendWidget::isValid() const {
	return true;
}

SendDataCommon::SentMessageFields SendWidget::getSentMessageFields() const {
	return { .text = _caption, .media = _media };
}

not_null<HistoryItem*> SendWidget::makeMessage(
		const Result *owner,
		not_null<History*> history,
		HistoryItemCommonFields &&fields) const {
	if (_invertMedia) {
		fields.flags |= MessageFlag::InvertMedia;
	}
	return SendDataCommon::makeMessage(owner, history, std::move(fields));
}

} // namespace internal
} // namespace InlineBots

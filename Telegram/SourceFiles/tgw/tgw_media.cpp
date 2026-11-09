/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_media.h"

#include "data/data_peer.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "history/view/media/history_view_html_widget.h"
#include "history/view/media/history_view_unsupported_widget.h"
#include "history/view/history_view_item_preview.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "tgw/tgw_state.h"
#include "ui/text/text_utilities.h"
#include "wallet/wallet_panel.h"

#include <zlib.h>

namespace Tgw {
namespace {

constexpr auto kInitialUnpackedSize = qsizetype(16 * 1024);

[[nodiscard]] TextWithEntities Stub() {
	return Ui::Text::Colorized(tr::lng_in_dlg_widget(tr::now));
}

[[nodiscard]] uInt ZlibSize(qsizetype size) {
	return uInt(std::min(
		uint64(size),
		uint64(std::numeric_limits<uInt>::max())));
}

[[nodiscard]] qsizetype GrownSize(qsizetype size) {
	const auto limit = QByteArray::maxSize();
	return (size > limit / 2) ? limit : (size * 2);
}

[[nodiscard]] ChatType ChatTypeOf(not_null<PeerData*> peer) {
	return peer->isUser()
		? ChatType::Private
		: peer->isChat()
		? ChatType::Group
		: peer->isMegagroup()
		? ChatType::Supergroup
		: ChatType::Channel;
}

} // namespace

QByteArray UnpackPayload(const QByteArray &payload) {
	if (payload.isEmpty()) {
		return QByteArray();
	}
	auto stream = z_stream();
	// The server sends zlib; older messages still need gzip support.
	if (inflateInit2(&stream, 32 + MAX_WBITS) != Z_OK) {
		return QByteArray();
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = QByteArray(kInitialUnpackedSize, Qt::Uninitialized);
	auto produced = qsizetype(0);
	auto fed = qsizetype(0);
	while (true) {
		if (!stream.avail_in && fed < payload.size()) {
			stream.avail_in = ZlibSize(payload.size() - fed);
			stream.next_in = reinterpret_cast<Bytef*>(
				const_cast<char*>(payload.constData() + fed));
			fed += qsizetype(stream.avail_in);
		}
		// No client cap: the server enforces payload limits and may move them.
		if (produced == result.size()) {
			if (result.size() >= QByteArray::maxSize()) {
				return QByteArray();
			}
			result.resize(GrownSize(result.size()));
		}
		const auto room = ZlibSize(result.size() - produced);
		stream.avail_out = room;
		stream.next_out = reinterpret_cast<Bytef*>(result.data() + produced);
		const auto code = inflate(&stream, Z_NO_FLUSH);
		produced += qsizetype(room - stream.avail_out);
		const auto consumed = !stream.avail_in && (fed == payload.size());
		if (code == Z_STREAM_END && consumed && produced > 0) {
			break;
		} else if (code != Z_OK || (consumed && stream.avail_out)) {
			return QByteArray();
		}
	}
	result.resize(produced);
	result.squeeze();
	return result;
}

WidgetFields ParseWidgetFields(const MTPDmessageMediaWidget &data) {
	const auto chat = data.vchat_data();
	const auto shared = data.vpublic_data();
	const auto hash = data.vpublic_data_hash();
	const auto id = data.vwidget_id();
	return {
		.anchors = {
			.width = data.vwidth().v,
			.height = data.vheight().v,
		},
		.payload = data.vpayload().v,
		.state = {
			.chatData = chat ? std::make_optional(chat->v) : std::nullopt,
			.publicData = shared ? std::make_optional(shared->v) : std::nullopt,
		},
		.publicDataHash = hash ? std::make_optional(hash->v) : std::nullopt,
		.widgetId = id ? std::make_optional(id->v) : std::nullopt,
		.flags = {
			.isCopy = data.is_is_copy(),
			.canQuery = data.is_can_query(),
			.backgroundQuery = data.is_background_query(),
		},
	};
}

WidgetMedia::WidgetMedia(not_null<HistoryItem*> parent, WidgetFields fields)
: Media(parent)
, _fields(std::move(fields)) {
	RegisterCarrier(this);
}

WidgetMedia::~WidgetMedia() {
	UnregisterCarrier(this);
}

std::unique_ptr<Data::Media> WidgetMedia::clone(
		not_null<HistoryItem*> parent) {
	auto result = std::make_unique<WidgetMedia>(parent, _fields);
	result->_html = _html;
	return result;
}

const WidgetFields &WidgetMedia::fields() const {
	return _fields;
}

const QByteArray &WidgetMedia::html() const {
	if (!_html) {
		_html = UnpackPayload(_fields.payload);
		if (_html->isEmpty()) {
			LOG(("Tgw Error: Could not unpack a widget payload of %1 bytes."
				).arg(_fields.payload.size()));
		}
	}
	return *_html;
}

MessageContext WidgetMedia::context() const {
	const auto item = parent();
	auto flags = _fields.flags;
	if (!item->isHistoryEntry()) {
		// Its requests could not name it in the user's coordinates.
		flags.canQuery = false;
		flags.backgroundQuery = false;
	}
	return {
		.flags = flags,
		.chatType = (flags.isCopy
			? ChatType::None
			: ChatTypeOf(item->history()->peer)),
		.wallet = (flags.canQuery
			&& Wallet::Available(&item->history()->session())),
	};
}

bool WidgetMedia::applyState(const WidgetStateEdit &edit) {
	auto &state = _fields.state;
	auto changed = false;
	if (edit.chatData && state.chatData != edit.chatData) {
		state.chatData = edit.chatData;
		changed = true;
	}
	if (edit.publicData
		&& (state.publicData != edit.publicData
			|| _fields.publicDataHash != edit.publicDataHash)) {
		state.publicData = edit.publicData;
		_fields.publicDataHash = edit.publicDataHash;
		changed = true;
	}
	return changed;
}

HistoryView::ItemPreview WidgetMedia::toPreview(
		ToPreviewOptions options) const {
	const auto caption = (options.hideCaption || options.ignoreMessageText)
		? tr::marked()
		: Dialogs::Ui::DialogsPreviewText(options.translated
			? parent()->translatedText()
			: parent()->originalText());
	return {
		.text = caption.empty() ? Stub() : st::wrap_rtl(caption),
	};
}

TextWithEntities WidgetMedia::notificationText() const {
	const auto &caption = parent()->originalText();
	return caption.empty() ? Stub() : caption;
}

QString WidgetMedia::pinnedTextSubstring() const {
	return parent()->originalText().empty()
		? (QChar(171) + tr::lng_in_dlg_widget(tr::now) + QChar(187))
		: QString();
}

TextForMimeData WidgetMedia::clipboardText() const {
	return TextForMimeData();
}

bool WidgetMedia::forceForwardedInfo() const {
	return true;
}

bool WidgetMedia::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool WidgetMedia::updateSentMedia(const MTPMessageMedia &media) {
	return media.match([&](const MTPDmessageMediaWidget &data) {
		return (ParseWidgetFields(data) == _fields);
	}, [](const auto &) {
		return false;
	});
}

std::unique_ptr<HistoryView::Media> WidgetMedia::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	const auto &html = this->html();
	if (!html.isEmpty()) {
		return std::make_unique<HistoryView::HtmlWidget>(
			message,
			realParent,
			_fields.anchors,
			html,
			context(),
			replacing);
	}
	return std::make_unique<HistoryView::UnsupportedWidget>(message);
}

} // namespace Tgw

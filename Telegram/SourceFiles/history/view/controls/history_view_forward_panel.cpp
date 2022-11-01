/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_forward_panel.h"

#include "history/history.h"
#include "history/history_message.h"
#include "history/history_item_components.h"
#include "history/view/history_view_item_preview.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_forum_topic.h"
#include "main/main_session.h"
#include "ui/chat/forward_options_box.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView::Controls {
namespace {

constexpr auto kUnknownVersion = -1;
constexpr auto kNameWithCaptionsVersion = -2;
constexpr auto kNameNoCaptionsVersion = -3;

} // namespace

ForwardPanel::ForwardPanel(Fn<void()> repaint)
: _repaint(std::move(repaint)) {
}

void ForwardPanel::update(
		Data::Thread *to,
		Data::ResolvedForwardDraft draft) {
	if (_to == to
		&& _data.items == draft.items
		&& _data.options == draft.options) {
		return;
	}
	_dataLifetime.destroy();
	_data = std::move(draft);
	_to = to;
	if (!empty()) {
		Assert(to != nullptr);

		_data.items.front()->history()->owner().itemRemoved(
		) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
			itemRemoved(item);
		}, _dataLifetime);

		if (const auto topic = _to->asTopic()) {
			topic->destroyed(
			) | rpl::start_with_next([=] {
				update(nullptr, {});
			}, _dataLifetime);
		}

		updateTexts();
	}
	_itemsUpdated.fire({});
}

rpl::producer<> ForwardPanel::itemsUpdated() const {
	return _itemsUpdated.events();
}

void ForwardPanel::checkTexts() {
	if (empty()) {
		return;
	}
	const auto keepNames = (_data.options
		== Data::ForwardOptions::PreserveInfo);
	const auto keepCaptions = (_data.options
		!= Data::ForwardOptions::NoNamesAndCaptions);
	auto version = keepNames
		? 0
		: keepCaptions
		? kNameWithCaptionsVersion
		: kNameNoCaptionsVersion;
	if (keepNames) {
		for (const auto item : _data.items) {
			if (const auto from = item->senderOriginal()) {
				version += from->nameVersion();
			} else if (const auto info = item->hiddenSenderInfo()) {
				++version;
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}
	}
	if (_nameVersion != version) {
		_nameVersion = version;
		updateTexts();
	}
}

void ForwardPanel::updateTexts() {
	const auto repainter = gsl::finally([&] {
		_repaint();
	});
	if (empty()) {
		_from.clear();
		_text.clear();
		return;
	}
	int32 version = 0;
	QString from;
	TextWithEntities text;
	const auto keepNames = (_data.options
		== Data::ForwardOptions::PreserveInfo);
	const auto keepCaptions = (_data.options
		!= Data::ForwardOptions::NoNamesAndCaptions);
	if (const auto count = int(_data.items.size())) {
		auto insertedPeers = base::flat_set<not_null<PeerData*>>();
		auto insertedNames = base::flat_set<QString>();
		auto fullname = QString();
		auto names = std::vector<QString>();
		names.reserve(_data.items.size());
		for (const auto item : _data.items) {
			if (const auto from = item->senderOriginal()) {
				if (!insertedPeers.contains(from)) {
					insertedPeers.emplace(from);
					names.push_back(from->shortName());
					fullname = from->name();
				}
			} else if (const auto info = item->hiddenSenderInfo()) {
				if (!insertedNames.contains(info->name)) {
					insertedNames.emplace(info->name);
					names.push_back(info->firstName);
					fullname = info->name;
				}
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}
		if (!keepNames) {
			from = tr::lng_forward_sender_names_removed(tr::now);
		} else if (names.size() > 2) {
			from = tr::lng_forwarding_from(
				tr::now,
				lt_count,
				names.size() - 1,
				lt_user,
				names[0]);
		} else if (names.size() < 2) {
			from = fullname;
		} else {
			from = tr::lng_forwarding_from_two(
				tr::now,
				lt_user,
				names[0],
				lt_second_user,
				names[1]);
		}

		if (count < 2) {
			const auto item = _data.items.front();
			text = item->toPreview({
				.hideSender = true,
				.hideCaption = !keepCaptions,
				.generateImages = false,
			}).text;
			const auto history = item->history();
			const auto dropCustomEmoji = !history->session().premium()
				&& !_to->peer()->isSelf()
				&& (item->computeDropForwardedInfo() || !keepNames);
			if (dropCustomEmoji) {
				text = DropCustomEmoji(std::move(text));
			}
		} else {
			text = Ui::Text::PlainLink(
				tr::lng_forward_messages(tr::now, lt_count, count));
		}
	}
	_from.setText(st::msgNameStyle, from, Ui::NameTextOptions());
	const auto context = Core::MarkedTextContext{
		.session = &_to->session(),
		.customEmojiRepaint = _repaint,
	};
	_text.setMarkedText(
		st::messageTextStyle,
		text,
		Ui::DialogTextOptions(),
		context);
}

void ForwardPanel::refreshTexts() {
	_nameVersion = kUnknownVersion;
	checkTexts();
}

void ForwardPanel::itemRemoved(not_null<const HistoryItem*> item) {
	const auto i = ranges::find(_data.items, item);
	if (i != end(_data.items)) {
		_data.items.erase(i);
		refreshTexts();
		_itemsUpdated.fire({});
	}
}

const HistoryItemsList &ForwardPanel::items() const {
	return _data.items;
}

bool ForwardPanel::empty() const {
	return _data.items.empty();
}

void ForwardPanel::editOptions(
		not_null<Window::SessionController*> controller) {
	using Options = Data::ForwardOptions;
	const auto now = _data.options;
	const auto count = _data.items.size();
	const auto dropNames = (now != Options::PreserveInfo);
	const auto hasCaptions = [&] {
		for (const auto item : _data.items) {
			if (const auto media = item->media()) {
				if (!item->originalText().text.isEmpty()
					&& media->allowsEditCaption()) {
					return true;
				}
			}
		}
		return false;
	}();
	const auto hasOnlyForcedForwardedInfo = [&] {
		if (hasCaptions) {
			return false;
		}
		for (const auto item : _data.items) {
			if (const auto media = item->media()) {
				if (!media->forceForwardedInfo()) {
					return false;
				}
			} else {
				return false;
			}
		}
		return true;
	}();
	const auto dropCaptions = (now == Options::NoNamesAndCaptions);
	const auto weak = base::make_weak(this);
	const auto changeRecipient = crl::guard(this, [=] {
		if (_data.items.empty()) {
			return;
		}
		auto data = base::take(_data);
		_to->owningHistory()->setForwardDraft(_to->topicRootId(), {});
		Window::ShowForwardMessagesBox(controller, {
			.ids = _to->owner().itemsToIds(data.items),
			.options = data.options,
		});
	});
	if (hasOnlyForcedForwardedInfo) {
		changeRecipient();
		return;
	}
	const auto optionsChanged = crl::guard(weak, [=](
			Ui::ForwardOptions options) {
		if (_data.items.empty()) {
			return;
		}
		const auto newOptions = (options.hasCaptions
			&& options.dropCaptions)
			? Options::NoNamesAndCaptions
			: options.dropNames
			? Options::NoSenderNames
			: Options::PreserveInfo;
		if (_data.options != newOptions) {
			_data.options = newOptions;
			_to->owningHistory()->setForwardDraft(_to->topicRootId(), {
				.ids = _to->owner().itemsToIds(_data.items),
				.options = newOptions,
			});
			_repaint();
		}
	});
	controller->show(Box(
		Ui::ForwardOptionsBox,
		count,
		Ui::ForwardOptions{
			.dropNames = dropNames,
			.hasCaptions = hasCaptions,
			.dropCaptions = dropCaptions,
		},
		optionsChanged,
		changeRecipient));
}

void ForwardPanel::paint(
		Painter &p,
		int x,
		int y,
		int available,
		int outerWidth) const {
	if (empty()) {
		return;
	}
	const_cast<ForwardPanel*>(this)->checkTexts();
	const auto firstItem = _data.items.front();
	const auto firstMedia = firstItem->media();
	const auto hasPreview = (_data.items.size() < 2)
		&& firstMedia
		&& firstMedia->hasReplyPreview();
	const auto preview = hasPreview ? firstMedia->replyPreview() : nullptr;
	if (preview) {
		auto to = QRect(
			x,
			y + st::msgReplyPadding.top(),
			st::msgReplyBarSize.height(),
			st::msgReplyBarSize.height());
		if (preview->width() == preview->height()) {
			p.drawPixmap(to.x(), to.y(), preview->pix());
		} else {
			auto from = (preview->width() > preview->height())
				? QRect(
					(preview->width() - preview->height()) / 2,
					0,
					preview->height(),
					preview->height())
				: QRect(
					0,
					(preview->height() - preview->width()) / 2,
					preview->width(),
					preview->width());
			p.drawPixmap(to, preview->pix(), from);
		}
		const auto skip = st::msgReplyBarSize.height()
			+ st::msgReplyBarSkip
			- st::msgReplyBarSize.width()
			- st::msgReplyBarPos.x();
		x += skip;
		available -= skip;
	}
	p.setPen(st::historyReplyNameFg);
	_from.drawElided(
		p,
		x,
		y + st::msgReplyPadding.top(),
		available);
	p.setPen(st::historyComposeAreaFg);
	_text.draw(p, {
		.position = QPoint(
			x,
			y + st::msgReplyPadding.top() + st::msgServiceNameFont->height),
		.availableWidth = available,
		.palette = &st::historyComposeAreaPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.elisionLines = 1,
	});
}

} // namespace HistoryView::Controls

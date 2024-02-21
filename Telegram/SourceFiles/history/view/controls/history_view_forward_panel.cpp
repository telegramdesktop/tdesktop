/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_forward_panel.h"

#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/history_item_components.h"
#include "history/view/history_view_item_preview.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_forum_topic.h"
#include "main/main_session.h"
#include "ui/chat/forward_options_box.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "core/ui_integration.h"
#include "lang/lang_keys.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

#include "apiwrap.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_changes.h"
#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace HistoryView::Controls {
namespace {

constexpr auto kUnknownVersion = -1;
constexpr auto kNameWithCaptionsVersion = -2;
constexpr auto kNameNoCaptionsVersion = -3;

[[nodiscard]] bool HasOnlyForcedForwardedInfo(const HistoryItemsList &list) {
	for (const auto &item : list) {
		if (const auto media = item->media()) {
			if (!media->forceForwardedInfo()) {
				return false;
			}
		} else {
			return false;
		}
	}
	return true;
}

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
			if (const auto from = item->originalSender()) {
				version += from->nameVersion();
			} else if (const auto info = item->originalHiddenSenderInfo()) {
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
			if (const auto from = item->originalSender()) {
				if (!insertedPeers.contains(from)) {
					insertedPeers.emplace(from);
					names.push_back(from->shortName());
					fullname = from->name();
				}
			} else if (const auto info = item->originalHiddenSenderInfo()) {
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
				.ignoreGroup = true,
			}).text;
			if (item->computeDropForwardedInfo() || !keepNames) {
				text = DropDisallowedCustomEmoji(_to->peer(), std::move(text));
			}
		} else {
			text = Ui::Text::Colorized(
				tr::lng_forward_messages(tr::now, lt_count, count));
		}
	}
	_from.setText(st::msgNameStyle, from, Ui::NameTextOptions());
	const auto context = Core::MarkedTextContext{
		.session = &_to->session(),
		.customEmojiRepaint = _repaint,
	};
	_text.setMarkedText(
		st::defaultTextStyle,
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

void ForwardPanel::editOptions(std::shared_ptr<ChatHelpers::Show> show) {
	using Options = Data::ForwardOptions;
	const auto now = _data.options;
	const auto count = _data.items.size();
	const auto dropNames = (now != Options::PreserveInfo);
	const auto sendersCount = ItemsForwardSendersCount(_data.items);
	const auto captionsCount = ItemsForwardCaptionsCount(_data.items);
	const auto hasOnlyForcedForwardedInfo = !captionsCount
		&& HasOnlyForcedForwardedInfo(_data.items);
	const auto dropCaptions = (now == Options::NoNamesAndCaptions);
	const auto weak = base::make_weak(this);
	const auto changeRecipient = crl::guard(this, [=] {
		if (_data.items.empty()) {
			return;
		}
		auto data = base::take(_data);
		_to->owningHistory()->setForwardDraft(_to->topicRootId(), {});
		Window::ShowForwardMessagesBox(show, {
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
		const auto newOptions = (options.captionsCount
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
	show->showBox(Box(
		Ui::ForwardOptionsBox,
		count,
		Ui::ForwardOptions{
			.sendersCount = sendersCount,
			.captionsCount = captionsCount,
			.dropNames = dropNames,
			.dropCaptions = dropCaptions,
		},
		optionsChanged,
		changeRecipient));
}

void ForwardPanel::editToNextOption() {
	using Options = Data::ForwardOptions;
	const auto captionsCount = ItemsForwardCaptionsCount(_data.items);
	const auto hasOnlyForcedForwardedInfo = !captionsCount
		&& HasOnlyForcedForwardedInfo(_data.items);
	if (hasOnlyForcedForwardedInfo) {
		return;
	}

	const auto now = _data.options;
	const auto next = (now == Options::PreserveInfo)
		? Options::NoSenderNames
		: ((now == Options::NoSenderNames) && captionsCount)
		? Options::NoNamesAndCaptions
		: Options::PreserveInfo;

	_to->owningHistory()->setForwardDraft(_to->topicRootId(), {
		.ids = _to->owner().itemsToIds(_data.items),
		.options = next,
	});
	_repaint();
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
	const auto now = crl::now();
	const auto paused = p.inactive();
	const auto pausedSpoiler = paused || On(PowerSaving::kChatSpoiler);
	const auto firstItem = _data.items.front();
	const auto firstMedia = firstItem->media();
	const auto hasPreview = (_data.items.size() < 2)
		&& firstMedia
		&& firstMedia->hasReplyPreview();
	const auto preview = hasPreview ? firstMedia->replyPreview() : nullptr;
	const auto spoiler = preview && firstMedia->hasSpoiler();
	if (!spoiler) {
		_spoiler = nullptr;
	} else if (!_spoiler) {
		_spoiler = std::make_unique<Ui::SpoilerAnimation>(_repaint);
	}
	if (preview) {
		auto to = QRect(
			x,
			y + (st::historyReplyHeight - st::historyReplyPreview) / 2,
			st::historyReplyPreview,
			st::historyReplyPreview);
		p.drawPixmap(to.x(), to.y(), preview->pixSingle(
			preview->size() / style::DevicePixelRatio(),
			{
				.options = Images::Option::RoundSmall,
				.outer = to.size(),
			}));
		if (_spoiler) {
			Ui::FillSpoilerRect(p, to, Ui::DefaultImageSpoiler().frame(
				_spoiler->index(now, pausedSpoiler)));
		}
		const auto skip = st::historyReplyPreview + st::msgReplyBarSkip;
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
		.now = now,
		.pausedEmoji = paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = pausedSpoiler,
		.elisionLines = 1,
	});
}

void ClearDraftReplyTo(
		not_null<History*> history,
		MsgId topicRootId,
		FullMsgId equalTo) {
	const auto local = history->localDraft(topicRootId);
	if (!local || (equalTo && local->reply.messageId != equalTo)) {
		return;
	}
	auto draft = *local;
	draft.reply = { .topicRootId = topicRootId };
	if (Data::DraftIsNull(&draft)) {
		history->clearLocalDraft(topicRootId);
	} else {
		history->setLocalDraft(
			std::make_unique<Data::Draft>(std::move(draft)));
	}
	if (const auto thread = history->threadFor(topicRootId)) {
		history->session().api().saveDraftToCloudDelayed(thread);
	}
}

void EditWebPageOptions(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<WebPageData*> webpage,
		Data::WebPageDraft draft,
		Fn<void(Data::WebPageDraft)> done) {
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Link Preview"_q));

		struct State {
			rpl::variable<Data::WebPageDraft> result;
			Ui::SettingsButton *large = nullptr;
			Ui::SettingsButton *small = nullptr;
		};
		const auto state = box->lifetime().make_state<State>(State{
			.result = draft,
			});

		state->large = Settings::AddButtonWithIcon(
			box->verticalLayout(),
			rpl::single(u"Force large media"_q),
			st::settingsButton,
			{ &st::menuIconMakeBig });
		state->large->setClickedCallback([=] {
			auto copy = state->result.current();
			copy.forceLargeMedia = true;
			copy.forceSmallMedia = false;
			state->result = copy;
		});

		state->small = Settings::AddButtonWithIcon(
			box->verticalLayout(),
			rpl::single(u"Force small media"_q),
			st::settingsButton,
			{ &st::menuIconMakeSmall });
		state->small->setClickedCallback([=] {
			auto copy = state->result.current();
			copy.forceSmallMedia = true;
			copy.forceLargeMedia = false;
			state->result = copy;
		});

		state->result.value(
		) | rpl::start_with_next([=](const Data::WebPageDraft &draft) {
			state->large->setColorOverride(draft.forceLargeMedia
				? st::windowActiveTextFg->c
				: std::optional<QColor>());
			state->small->setColorOverride(draft.forceSmallMedia
				? st::windowActiveTextFg->c
				: std::optional<QColor>());
		}, box->lifetime());

		Settings::AddButtonWithIcon(
			box->verticalLayout(),
			state->result.value(
			) | rpl::map([=](const Data::WebPageDraft &draft) {
				return draft.invert
					? u"Above message"_q
					: u"Below message"_q;
			}),
			st::settingsButton,
			{ &st::menuIconChangeOrder }
		)->setClickedCallback([=] {
			auto copy = state->result.current();
			copy.invert = !copy.invert;
			state->result = copy;
		});

		box->addButton(tr::lng_settings_save(), [=] {
			const auto weak = Ui::MakeWeak(box.get());
			auto result = state->result.current();
			result.manual = true;
			done(result);
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
		});
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));

}

} // namespace HistoryView::Controls

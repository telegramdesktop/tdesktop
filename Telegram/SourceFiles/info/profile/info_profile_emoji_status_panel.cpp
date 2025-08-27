/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_emoji_status_panel.h"

#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "lang/lang_keys.h"
#include "menu/menu_send.h" // SendMenu::Type.
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/time_picker_box.h"
#include "ui/effects/emoji_fly_animation.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "base/unixtime.h"
#include "boxes/premium_preview_box.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "styles/style_chat_helpers.h"

namespace Info::Profile {
namespace {

constexpr auto kLimitFirstRow = 8;

void PickUntilBox(not_null<Ui::GenericBox*> box, Fn<void(TimeId)> callback) {
	box->setTitle(tr::lng_emoji_status_for_title());

	const auto seconds = Ui::DefaultTimePickerValues();
	const auto phrases = ranges::views::all(
		seconds
	) | ranges::views::transform(Ui::FormatMuteFor) | ranges::to_vector;

	const auto pickerCallback = Ui::TimePickerBox(box, seconds, phrases, 0);

	Ui::ConfirmBox(box, {
		.confirmed = [=] {
			callback(pickerCallback());
			box->closeBox();
		},
		.confirmText = tr::lng_emoji_status_for_submit(),
		.cancelText = tr::lng_cancel(),
	});
}

} // namespace

EmojiStatusPanel::EmojiStatusPanel() = default;

EmojiStatusPanel::~EmojiStatusPanel() {
	if (hasFocus()) {
		// Panel will try to return focus to the layer widget, the problem is
		// we are destroying the layer widget probably right now and focusing
		// it will lead to a crash, because it destroys its children (how we
		// got here) after it clears focus out of itself. So if you return
		// the focus inside a child destructor, it won't be cleared at all.
		_panel->window()->setFocus();
	}
}

void EmojiStatusPanel::setChooseFilter(Fn<bool(EmojiStatusId)> filter) {
	_chooseFilter = std::move(filter);
}

void EmojiStatusPanel::show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> button,
		Data::CustomEmojiSizeTag animationSizeTag) {
	show({
		.controller = controller,
		.button = button,
		.animationSizeTag = animationSizeTag,
		.ensureAddedEmojiId = controller->session().user()->emojiStatusId(),
		.withCollectibles = true,
	});
}

void EmojiStatusPanel::show(Descriptor &&descriptor) {
	const auto controller = descriptor.controller;
	if (!_panel) {
		create(descriptor);

		_panel->shownValue(
		) | rpl::filter([=] {
			return (_panelButton != nullptr);
		}) | rpl::start_with_next([=](bool shown) {
			if (shown) {
				_panelButton->installEventFilter(_panel.get());
			} else {
				_panelButton->removeEventFilter(_panel.get());
			}
		}, _panel->lifetime());
	}
	const auto button = descriptor.button;
	if (const auto previous = _panelButton.data()) {
		if (previous != button) {
			previous->removeEventFilter(_panel.get());
		}
	}
	_panelButton = button;
	_animationSizeTag = descriptor.animationSizeTag;
	const auto feed = [=, now = descriptor.ensureAddedEmojiId](
			std::vector<EmojiStatusId> list) {
		list.insert(begin(list), EmojiStatusId());
		if (now && !ranges::contains(list, now)) {
			list.push_back(now);
		}
		_panel->selector()->provideRecentEmoji(list);
	};
	if (descriptor.backgroundEmojiMode) {
		controller->session().api().peerPhoto().emojiListValue(
			Api::PeerPhoto::EmojiListType::Background
		) | rpl::start_with_next([=](std::vector<DocumentId> &&list) {
			auto tmp = std::vector<EmojiStatusId>();
			for (const auto &id : list) {
				tmp.push_back(EmojiStatusId{ .documentId = id });
			}
			feed(std::move(tmp));
		}, _panel->lifetime());
	} else if (descriptor.channelStatusMode) {
		const auto &statuses = controller->session().data().emojiStatuses();
		const auto &other = statuses.list(Data::EmojiStatuses::Type::ChannelDefault);
		auto list = statuses.list(Data::EmojiStatuses::Type::ChannelColored);
		if (list.size() > kLimitFirstRow - 1) {
			list.erase(begin(list) + kLimitFirstRow - 1, end(list));
		}
		list.reserve(list.size() + other.size() + 1);
		for (const auto &id : other) {
			if (!ranges::contains(list, id)) {
				list.push_back(id);
			}
		}
		feed(std::move(list));
	} else {
		const auto &statuses = controller->session().data().emojiStatuses();
		const auto &recent = statuses.list(Data::EmojiStatuses::Type::Recent);
		const auto &other = statuses.list(Data::EmojiStatuses::Type::Default);
		auto list = statuses.list(Data::EmojiStatuses::Type::Colored);
		if (list.size() > kLimitFirstRow - 1) {
			list.erase(begin(list) + kLimitFirstRow - 1, end(list));
		}
		list.reserve(list.size() + recent.size() + other.size() + 1);
		for (const auto &id : ranges::views::concat(recent, other)) {
			if (!ranges::contains(list, id)) {
				list.push_back(id);
			}
		}
		feed(std::move(list));
	}
	const auto parent = _panel->parentWidget();
	const auto global = button->mapToGlobal(QPoint());
	const auto local = parent->mapFromGlobal(global);
	if (descriptor.backgroundEmojiMode || descriptor.channelStatusMode) {
		_panel->moveBottomRight(
			local.y() + (st::normalFont->height / 2),
			local.x() + button->width() * 3);
	} else {
		_panel->moveTopRight(
			local.y() + button->height() - (st::normalFont->height / 2),
			local.x() + button->width() * 3);
	}
	_panel->toggleAnimated();
}

bool EmojiStatusPanel::hasFocus() const {
	return _panel && Ui::InFocusChain(_panel.get());
}

void EmojiStatusPanel::repaint() {
	_panel->selector()->update();
}

bool EmojiStatusPanel::paintBadgeFrame(not_null<Ui::RpWidget*> widget) {
	if (!_animation) {
		return false;
	} else if (_animation->paintBadgeFrame(widget)) {
		return true;
	}
	InvokeQueued(_animation->layer(), [=] { _animation = nullptr; });
	return false;
}

void EmojiStatusPanel::create(const Descriptor &descriptor) {
	using Selector = ChatHelpers::TabbedSelector;
	using Descriptor = ChatHelpers::TabbedSelectorDescriptor;
	using Mode = ChatHelpers::TabbedSelector::Mode;
	const auto controller = descriptor.controller;
	const auto body = controller->window().widget()->bodyWidget();
	auto features = ChatHelpers::ComposeFeatures();
	features.collectibleStatus = descriptor.withCollectibles;
	_panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		body,
		controller,
		object_ptr<Selector>(
			nullptr,
			Descriptor{
				.show = controller->uiShow(),
				.st = ((descriptor.backgroundEmojiMode
					|| descriptor.channelStatusMode)
					? st::backgroundEmojiPan
					: st::statusEmojiPan),
				.level = Window::GifPauseReason::Layer,
				.mode = (descriptor.backgroundEmojiMode
					? Mode::BackgroundEmoji
					: descriptor.channelStatusMode
					? Mode::ChannelStatus
					: Mode::EmojiStatus),
				.customTextColor = descriptor.customTextColor,
				.features = features,
			}));
	_customTextColor = descriptor.customTextColor;
	_backgroundEmojiMode = descriptor.backgroundEmojiMode;
	_channelStatusMode = descriptor.channelStatusMode;
	_panel->setDropDown(!_backgroundEmojiMode && !_channelStatusMode);
	_panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_panel->hide();

	struct Chosen {
		EmojiStatusId id;
		TimeId until = 0;
		Ui::MessageSendingAnimationFrom animation;
	};

	_panel->selector()->contextMenuRequested(
	) | rpl::start_with_next([=] {
		_panel->selector()->showMenuWithDetails({});
	}, _panel->lifetime());

	auto statusChosen = _panel->selector()->customEmojiChosen(
	) | rpl::map([=](ChatHelpers::FileChosen data) {
		return Chosen{
			.id = {
				data.collectible ? DocumentId() : data.document->id,
				data.collectible,
			},
			.until = data.options.scheduled,
			.animation = data.messageSendingFrom,
		};
	});

	auto emojiChosen = _panel->selector()->emojiChosen(
	) | rpl::map([=](ChatHelpers::EmojiChosen data) {
		return Chosen{ .animation = data.messageSendingFrom };
	});

	if (descriptor.backgroundEmojiMode || descriptor.channelStatusMode) {
		rpl::merge(
			std::move(statusChosen),
			std::move(emojiChosen)
		) | rpl::start_with_next([=](const Chosen &chosen) {
			const auto owner = &controller->session().data();
			startAnimation(owner, body, chosen.id, chosen.animation);
			_someCustomChosen.fire({ chosen.id, chosen.until });
			_panel->hideAnimated();
		}, _panel->lifetime());
	} else {
		const auto weak = base::make_weak(_panel.get());
		const auto accept = [=](Chosen chosen) {
			Expects(chosen.until != Selector::kPickCustomTimeId);

			// PickUntilBox calls this after EmojiStatusPanel is destroyed!
			const auto owner = &controller->session().data();
			if (weak) {
				startAnimation(owner, body, chosen.id, chosen.animation);
			}
			owner->emojiStatuses().set(chosen.id, chosen.until);
		};

		rpl::merge(
			std::move(statusChosen),
			std::move(emojiChosen)
		) | rpl::filter([=](const Chosen &chosen) {
			return filter(controller, chosen.id);
		}) | rpl::start_with_next([=](const Chosen &chosen) {
			if (chosen.until == Selector::kPickCustomTimeId) {
				_panel->hideAnimated();
				controller->show(Box(PickUntilBox, [=](TimeId seconds) {
					accept({ chosen.id, base::unixtime::now() + seconds });
				}));
			} else {
				accept(chosen);
				_panel->hideAnimated();
			}
		}, _panel->lifetime());
	}
}

bool EmojiStatusPanel::filter(
		not_null<Window::SessionController*> controller,
		EmojiStatusId chosenId) const {
	if (_chooseFilter) {
		return _chooseFilter(chosenId);
	} else if (chosenId && !controller->session().premium()) {
		ShowPremiumPreviewBox(controller, PremiumFeature::EmojiStatus);
		return false;
	}
	return true;
}

void EmojiStatusPanel::startAnimation(
		not_null<Data::Session*> owner,
		not_null<Ui::RpWidget*> body,
		EmojiStatusId statusId,
		Ui::MessageSendingAnimationFrom from) {
	if (!_panelButton || !statusId) {
		return;
	}
	const auto documentId = statusId.collectible
		? statusId.collectible->documentId
		: statusId.documentId;
	auto args = Ui::ReactionFlyAnimationArgs{
		.id = { { documentId } },
		.flyIcon = from.frame,
		.flyFrom = body->mapFromGlobal(from.globalStartGeometry),
		.forceFirstFrame = _backgroundEmojiMode,
	};
	const auto color = _customTextColor
		? _customTextColor
		: [] { return st::profileVerifiedCheckBg->c; };
	_animation = std::make_unique<Ui::EmojiFlyAnimation>(
		body,
		&owner->reactions(),
		std::move(args),
		[=] { _animation->repaint(); },
		_customTextColor,
		_animationSizeTag);
}

} // namespace Info::Profile

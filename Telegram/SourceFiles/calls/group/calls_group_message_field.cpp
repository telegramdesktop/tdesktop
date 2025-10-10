/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_message_field.h"

#include "base/event_filter.h"
#include "boxes/premium_preview_box.h"
#include "calls/group/calls_group_messages.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "history/view/reactions/history_view_reactions_strip.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/send_button.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"
#include "styles/style_calls.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat.h"
#include "styles/style_media_view.h"

namespace Calls::Group {
namespace {

constexpr auto kErrorLimit = 99;

using Chosen = HistoryView::Reactions::ChosenReaction;

} // namespace

class ReactionPanel final {
public:
	ReactionPanel(
		not_null<QWidget*> outer,
		std::shared_ptr<ChatHelpers::Show> show,
		rpl::producer<QRect> fieldGeometry);
	~ReactionPanel();

	[[nodiscard]] rpl::producer<Chosen> chosen() const;

	void show();
	void hide();
	void raise();
	void hideIfCollapsed();
	void collapse();

private:
	struct Hiding;

	void create();
	void updateShowState();
	void fadeOutSelector();
	void startAnimation();

	const not_null<QWidget*> _outer;
	const std::shared_ptr<ChatHelpers::Show> _show;
	std::unique_ptr<Ui::RpWidget> _parent;
	std::unique_ptr<HistoryView::Reactions::Selector> _selector;
	std::vector<std::unique_ptr<Hiding>> _hiding;
	rpl::event_stream<Chosen> _chosen;
	Ui::Animations::Simple _showing;
	rpl::variable<float64> _shownValue;
	rpl::variable<QRect> _fieldGeometry;
	rpl::variable<bool> _expanded;
	rpl::variable<bool> _shown = false;

};

struct ReactionPanel::Hiding {
	explicit Hiding(not_null<QWidget*> parent) : widget(parent) {
	}

	Ui::RpWidget widget;
	Ui::Animations::Simple animation;
	QImage frame;
};

ReactionPanel::ReactionPanel(
	not_null<QWidget*> outer,
	std::shared_ptr<ChatHelpers::Show> show,
	rpl::producer<QRect> fieldGeometry)
: _outer(outer)
, _show(std::move(show))
, _fieldGeometry(std::move(fieldGeometry)) {
}

ReactionPanel::~ReactionPanel() = default;

auto ReactionPanel::chosen() const -> rpl::producer<Chosen> {
	return _chosen.events();
}

void ReactionPanel::show() {
	if (_shown.current()) {
		return;
	}
	create();
	if (!_selector) {
		return;
	}
	const auto duration = st::defaultPanelAnimation.heightDuration
		* st::defaultPopupMenu.showDuration;
	_shown = true;
	_showing.start([=] { updateShowState(); }, 0., 1., duration);
	updateShowState();
	_parent->show();
}

void ReactionPanel::hide() {
	if (!_selector) {
		return;
	}
	_selector->beforeDestroy();
	if (!anim::Disabled()) {
		fadeOutSelector();
	}
	_shown = false;
	_expanded = false;
	_showing.stop();
	_selector = nullptr;
	_parent = nullptr;
}

void ReactionPanel::raise() {
	if (_parent) {
		_parent->raise();
	}
}

void ReactionPanel::hideIfCollapsed() {
	if (!_expanded.current()) {
		hide();
	}
}

void ReactionPanel::collapse() {
	if (_expanded.current()) {
		hide();
		show();
	}
}

void ReactionPanel::create() {
	auto reactions = Data::LookupPossibleReactions(&_show->session());
	if (reactions.recent.empty()) {
		return;
	}
	_parent = std::make_unique<Ui::RpWidget>(_outer);
	_parent->show();

	_parent->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseButtonPress) {
			const auto event = static_cast<QMouseEvent*>(e.get());
			if (event->button() == Qt::LeftButton) {
				if (!_selector
					|| !_selector->geometry().contains(event->pos())) {
					collapse();
				}
			}
		}
	}, _parent->lifetime());

	_selector = std::make_unique<HistoryView::Reactions::Selector>(
		_parent.get(),
		st::storiesReactionsPan,
		_show,
		std::move(reactions),
		TextWithEntities(),
		[=](bool fast) { hide(); },
		nullptr, // iconFactory
		nullptr, // paused
		true);

	_selector->chosen(
	) | rpl::start_with_next([=](Chosen reaction) {
		if (reaction.id.custom() && !_show->session().premium()) {
			ShowPremiumPreviewBox(
				_show,
				PremiumFeature::AnimatedEmoji);
		} else {
			_chosen.fire(std::move(reaction));
			hide();
		}
	}, _selector->lifetime());

	const auto desiredWidth = st::storiesReactionsWidth;
	const auto maxWidth = desiredWidth * 2;
	const auto width = _selector->countWidth(desiredWidth, maxWidth);
	const auto margins = _selector->marginsForShadow();
	const auto categoriesTop = _selector->extendTopForCategoriesAndAbout(
		width);
	const auto full = margins.left() + width + margins.right();

	_shownValue = 0.;
	rpl::combine(
		_fieldGeometry.value(),
		_shownValue.value(),
		_expanded.value()
	) | rpl::start_with_next([=](QRect field, float64 shown, bool expanded) {
		const auto width = margins.left()
			+ _selector->countAppearedWidth(shown)
			+ margins.right();
		const auto available = field.y();
		const auto min = st::storiesReactionsBottomSkip
			+ st::reactStripHeight;
		const auto max = min
			+ margins.top()
			+ categoriesTop
			+ st::storiesReactionsAddedTop;
		const auto height = expanded ? std::min(available, max) : min;
		const auto top = field.y() - height;
		const auto shift = (width / 2);
		const auto right = (field.x() + field.width() / 2 + shift);
		_parent->setGeometry(QRect((right - width), top, full, height));
		const auto innerTop = height
			- st::storiesReactionsBottomSkip
			- st::reactStripHeight;
		const auto maxAdded = innerTop - margins.top() - categoriesTop;
		const auto added = std::min(maxAdded, st::storiesReactionsAddedTop);
		_selector->setSpecialExpandTopSkip(added);
		_selector->initGeometry(innerTop);
	}, _selector->lifetime());

	_selector->willExpand(
	) | rpl::start_with_next([=] {
		_expanded = true;

		const auto raw = _parent.get();
		base::install_event_filter(raw, qApp, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::MouseButtonPress) {
				const auto event = static_cast<QMouseEvent*>(e.get());
				if (event->button() == Qt::LeftButton) {
					if (!_selector
						|| !_selector->geometry().contains(
							_parent->mapFromGlobal(event->globalPos()))) {
						collapse();
					}
				}
			}
			return base::EventFilterResult::Continue;
		});
	}, _selector->lifetime());

	_selector->escapes() | rpl::start_with_next([=] {
		collapse();
	}, _selector->lifetime());
}

void ReactionPanel::fadeOutSelector() {
	const auto geometry = Ui::MapFrom(
		_outer,
		_parent.get(),
		_selector->geometry());
	_hiding.push_back(std::make_unique<Hiding>(_outer));
	const auto raw = _hiding.back().get();
	raw->frame = Ui::GrabWidgetToImage(_selector.get());
	raw->widget.setGeometry(geometry);
	raw->widget.show();
	raw->widget.paintRequest(
	) | rpl::start_with_next([=] {
		if (const auto opacity = raw->animation.value(0.)) {
			auto p = QPainter(&raw->widget);
			p.setOpacity(opacity);
			p.drawImage(0, 0, raw->frame);
		}
	}, raw->widget.lifetime());
	Ui::PostponeCall(&raw->widget, [=] {
		raw->animation.start([=] {
			if (raw->animation.animating()) {
				raw->widget.update();
			} else {
				const auto i = ranges::find(
					_hiding,
					raw,
					&std::unique_ptr<Hiding>::get);
				if (i != end(_hiding)) {
					_hiding.erase(i);
				}
			}
		}, 1., 0., st::slideWrapDuration);
	});
}

void ReactionPanel::updateShowState() {
	const auto progress = _showing.value(_shown.current() ? 1. : 0.);
	const auto opacity = 1.;
	const auto appearing = _showing.animating();
	const auto toggling = false;
	_shownValue = progress;
	_selector->updateShowState(progress, opacity, appearing, toggling);
}

MessageField::MessageField(
	not_null<QWidget*> parent,
	std::shared_ptr<ChatHelpers::Show> show,
	PeerData *peer)
: _parent(parent)
, _show(std::move(show))
, _wrap(std::make_unique<Ui::RpWidget>(_parent))
, _limit(_show->session().appConfig().groupCallMessageLengthLimit()) {
	createControls(peer);
}

MessageField::~MessageField() = default;

void MessageField::createControls(PeerData *peer) {
	setupBackground();

	const auto &st = st::storiesComposeControls;
	_field = Ui::CreateChild<Ui::InputField>(
		_wrap.get(),
		st.field,
		Ui::InputField::Mode::MultiLine,
		tr::lng_message_ph());
	_field->setMaxLength(_limit + kErrorLimit);
	_field->setMinHeight(
		st::historySendSize.height() - 2 * st::historySendPadding);
	_field->setMaxHeight(st::historyComposeFieldMaxHeight);
	_field->setDocumentMargin(4.);
	_field->setAdditionalMargin(style::ConvertScale(4) - 4);

	_reactionPanel = std::make_unique<ReactionPanel>(
		_parent,
		_show,
		_wrap->geometryValue());
	_fieldFocused = _field->focusedChanges();
	_fieldEmpty = _field->changes() | rpl::map([field = _field] {
		return field->getLastText().trimmed().isEmpty();
	});
	rpl::combine(
		_fieldFocused.value(),
		_fieldEmpty.value()
	) | rpl::start_with_next([=](bool focused, bool empty) {
		if (!focused) {
			_reactionPanel->hideIfCollapsed();
		} else if (empty) {
			_reactionPanel->show();
		} else {
			_reactionPanel->hide();
		}
	}, _field->lifetime());

	_reactionPanel->chosen(
	) | rpl::start_with_next([=](Chosen reaction) {
		if (const auto customId = reaction.id.custom()) {
			const auto document = _show->session().data().document(customId);
			if (const auto sticker = document->sticker()) {
				if (const auto alt = sticker->alt; !alt.isEmpty()) {
					const auto length = int(alt.size());
					const auto data = Data::SerializeCustomEmojiId(customId);
					const auto tag = Ui::InputField::CustomEmojiLink(data);
					_submitted.fire({ alt, { { 0, length, tag } } });
				}
			}
		} else {
			_submitted.fire({ reaction.id.emoji() });
		}
		_reactionPanel->hide();
	}, _field->lifetime());

	const auto show = _show;
	const auto allow = [=](not_null<DocumentData*> emoji) {
		if (peer && Data::AllowEmojiWithoutPremium(peer, emoji)) {
			return true;
		}
		return false;
	};
	InitMessageFieldHandlers({
		.session = &show->session(),
		.show = show,
		.field = _field,
		.customEmojiPaused = [=] {
			return show->paused(ChatHelpers::PauseReason::Layer);
		},
		.allowPremiumEmoji = allow,
		.fieldStyle = &st.files.caption,
		.allowMarkdownTags = {
			Ui::InputField::kTagBold,
			Ui::InputField::kTagItalic,
			Ui::InputField::kTagUnderline,
			Ui::InputField::kTagStrikeOut,
			Ui::InputField::kTagSpoiler,
		},
	});
	Ui::Emoji::SuggestionsController::Init(
		_parent,
		_field,
		&_show->session(),
		{
			.suggestCustomEmoji = true,
			.allowCustomWithoutPremium = allow,
			.st = &st.suggestions,
		});

	_send = Ui::CreateChild<Ui::SendButton>(_wrap.get(), st.send);
	_send->show();

	using Selector = ChatHelpers::TabbedSelector;
	_emojiPanel = std::make_unique<ChatHelpers::TabbedPanel>(
		_parent,
		ChatHelpers::TabbedPanelDescriptor{
			.ownedSelector = object_ptr<Selector>(
				nullptr,
				ChatHelpers::TabbedSelectorDescriptor{
					.show = _show,
					.st = st.tabbed,
					.level = ChatHelpers::PauseReason::Layer,
					.mode = ChatHelpers::TabbedSelector::Mode::EmojiOnly,
					.features = {
						.stickersSettings = false,
						.openStickerSets = false,
					},
				}),
		});
	const auto panel = _emojiPanel.get();
	panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	panel->hide();
	panel->selector()->setCurrentPeer(peer);
	panel->selector()->emojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), data.emoji);
	}, lifetime());
	panel->selector()->customEmojiChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		const auto info = data.document->sticker();
		if (info
			&& info->setType == Data::StickersType::Emoji
			&& !_show->session().premium()) {
			ShowPremiumPreviewBox(
				_show,
				PremiumFeature::AnimatedEmoji);
		} else {
			Data::InsertCustomEmoji(_field, data.document);
		}
	}, lifetime());

	_emojiToggle = Ui::CreateChild<Ui::EmojiButton>(_wrap.get(), st.emoji);
	_emojiToggle->show();

	_emojiToggle->installEventFilter(panel);
	_emojiToggle->addClickHandler([=] {
		panel->toggleAnimated();
	});

	_width.value(
	) | rpl::filter(
		rpl::mappers::_1 > 0
	) | rpl::start_with_next([=](int newWidth) {
		const auto fieldWidth = newWidth
			- st::historySendPadding
			- _emojiToggle->width()
			- _send->width();
		_field->resizeToWidth(fieldWidth);
		_field->moveToLeft(
			st::historySendPadding,
			st::historySendPadding,
			newWidth);
		updateWrapSize(newWidth);
	}, _lifetime);

	rpl::combine(
		_width.value(),
		_field->heightValue()
	) | rpl::start_with_next([=](int width, int height) {
		if (width <= 0) {
			return;
		}
		const auto minHeight = st::historySendSize.height()
			- 2 * st::historySendPadding;
		_send->moveToRight(0, height - minHeight, width);
		_emojiToggle->moveToRight(_send->width(), height - minHeight, width);
		updateWrapSize();
	}, _lifetime);

	_field->cancelled() | rpl::start_with_next([=] {
		_closeRequests.fire({});
	}, _lifetime);

	const auto updateLimitPosition = [=](QSize parent, QSize label) {
		const auto skip = st::historySendPadding;
		return QPoint(parent.width() - label.width() - skip, skip);
	};
	Ui::AddLengthLimitLabel(_field, _limit, {
		.customParent = _wrap.get(),
		.customUpdatePosition = updateLimitPosition,
	});

	rpl::merge(
		_field->submits() | rpl::to_empty,
		_send->clicks() | rpl::to_empty
	) | rpl::start_with_next([=] {
		auto text = _field->getTextWithTags();
		if (text.text.size() <= _limit) {
			_submitted.fire(std::move(text));
		}
	}, _lifetime);
}

void MessageField::updateEmojiPanelGeometry() {
	const auto global = _emojiToggle->mapToGlobal({ 0, 0 });
	const auto local = _parent->mapFromGlobal(global);
	_emojiPanel->moveBottomRight(
		local.y(),
		local.x() + _emojiToggle->width() * 3);
}

void MessageField::setupBackground() {
	_wrap->paintRequest() | rpl::start_with_next([=] {
		const auto radius = st::historySendSize.height() / 2.;
		auto p = QPainter(_wrap.get());
		auto hq = PainterHighQualityEnabler(p);

		p.setPen(Qt::NoPen);
		p.setBrush(st::storiesComposeBg);
		p.drawRoundedRect(_wrap->rect(), radius, radius);
	}, _lifetime);
}

void MessageField::resizeToWidth(int newWidth) {
	_width = newWidth;
	if (_wrap->isHidden()) {
		Ui::SendPendingMoveResizeEvents(_wrap.get());
	}
	updateEmojiPanelGeometry();
}

void MessageField::move(int x, int y) {
	_wrap->move(x, y);
	if (_cache) {
		_cache->move(x, y);
	}
}

void MessageField::toggle(bool shown) {
	if (_shown == shown) {
		return;
	} else if (shown) {
		Assert(_width.current() > 0);
		Ui::SendPendingMoveResizeEvents(_wrap.get());
	} else if (Ui::InFocusChain(_field)) {
		_parent->setFocus();
	}
	_shown = shown;
	if (!anim::Disabled()) {
		if (!_cache) {
			auto image = Ui::GrabWidgetToImage(_wrap.get());
			_cache = std::make_unique<Ui::RpWidget>(_parent);
			const auto raw = _cache.get();
			raw->paintRequest() | rpl::start_with_next([=] {
				auto p = QPainter(raw);
				auto hq = PainterHighQualityEnabler(p);
				const auto scale = raw->height() / float64(_wrap->height());
				const auto target = _wrap->rect();
				const auto center = target.center();
				p.translate(center);
				p.scale(scale, scale);
				p.translate(-center);
				p.drawImage(target, image);
			}, raw->lifetime());
			raw->show();
			raw->move(_wrap->pos());
			raw->resize(_wrap->width(), 0);

			_wrap->hide();
		}
		_shownAnimation.start(
			[=] { shownAnimationCallback(); },
			shown ? 0. : 1.,
			shown ? 1. : 0.,
			st::slideWrapDuration,
			anim::easeOutCirc);
	}
	shownAnimationCallback();
}

void MessageField::raise() {
	_wrap->raise();
	if (_cache) {
		_cache->raise();
	}
	if (_reactionPanel) {
		_reactionPanel->raise();
	}
	if (_emojiPanel) {
		_emojiPanel->raise();
	}
}

void MessageField::updateWrapSize(int widthOverride) {
	const auto width = widthOverride ? widthOverride : _wrap->width();
	const auto height = _field->height() + 2 * st::historySendPadding;
	_wrap->resize(width, height);
	updateHeight();
}

void MessageField::updateHeight() {
	_height = int(base::SafeRound(
		_shownAnimation.value(_shown ? 1. : 0.) * _wrap->height()));
}

void MessageField::shownAnimationCallback() {
	updateHeight();
	if (_shownAnimation.animating()) {
		Assert(_cache != nullptr);
		_cache->resize(_cache->width(), _height.current());
		_cache->update();
	} else if (_shown) {
		_cache = nullptr;
		_wrap->show();
		_field->setFocusFast();
	} else {
		_closed.fire({});
	}
}

int MessageField::height() const {
	return _height.current();
}

rpl::producer<int> MessageField::heightValue() const {
	return _height.value();
}

rpl::producer<TextWithTags> MessageField::submitted() const {
	return _submitted.events();
}

rpl::producer<> MessageField::closeRequests() const {
	return _closeRequests.events();
}

rpl::producer<> MessageField::closed() const {
	return _closed.events();
}

rpl::lifetime &MessageField::lifetime() {
	return _lifetime;
}

} // namespace Calls::Group

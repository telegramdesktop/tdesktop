/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_forum_topic_box.h"

#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/emoji_fly_animation.h"
#include "ui/abstract_button.h"
#include "ui/vertical_list.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_forum.h"
#include "data/data_forum_icons.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "base/event_filter.h"
#include "base/random.h"
#include "base/qt_signal_producer.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_footer.h"
#include "boxes/premium_preview_box.h"
#include "main/main_session.h"
#include "history/history.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_sticker_toast.h"
#include "lang/lang_keys.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "styles/style_layers.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kDefaultIconId = DocumentId(0x7FFF'FFFF'FFFF'FFFFULL);

using DefaultIcon = Data::TopicIconDescriptor;

class DefaultIconEmoji final : public Ui::Text::CustomEmoji {
public:
	DefaultIconEmoji(
		rpl::producer<DefaultIcon> value,
		Fn<void()> repaint,
		Data::CustomEmojiSizeTag tag);

	int width() override;
	QString entityData() override;

	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	DefaultIcon _icon = {};
	QImage _image;
	Data::CustomEmojiSizeTag _tag = {};

	rpl::lifetime _lifetime;

};

DefaultIconEmoji::DefaultIconEmoji(
	rpl::producer<DefaultIcon> value,
	Fn<void()> repaint,
	Data::CustomEmojiSizeTag tag)
: _tag(tag) {
	std::move(value) | rpl::start_with_next([=](DefaultIcon value) {
		_icon = value;
		_image = QImage();
		if (repaint) {
			repaint();
		}
	}, _lifetime);
}

int DefaultIconEmoji::width() {
	return st::emojiSize + 2 * st::emojiPadding;
}

QString DefaultIconEmoji::entityData() {
	return u"topic_icon:%1"_q.arg(_icon.colorId);
}

void DefaultIconEmoji::paint(QPainter &p, const Context &context) {
	const auto &st = (_tag == Data::CustomEmojiSizeTag::Normal)
		? st::normalForumTopicIcon
		: st::defaultForumTopicIcon;
	const auto general = Data::IsForumGeneralIconTitle(_icon.title);
	if (_image.isNull()) {
		_image = general
			? Data::ForumTopicGeneralIconFrame(
				st.size,
				QColor(255, 255, 255))
			: Data::ForumTopicIconFrame(_icon.colorId, _icon.title, st);
	}
	const auto full = (_tag == Data::CustomEmojiSizeTag::Normal)
		? Ui::Emoji::GetSizeNormal()
		: Ui::Emoji::GetSizeLarge();
	const auto esize = full / style::DevicePixelRatio();
	const auto customSize = Ui::Text::AdjustCustomEmojiSize(esize);
	const auto skip = (customSize - st.size) / 2;
	p.drawImage(context.position + QPoint(skip, skip), general
		? style::colorizeImage(_image, context.textColor)
		: _image);
}

void DefaultIconEmoji::unload() {
	_image = QImage();
}

bool DefaultIconEmoji::ready() {
	return true;
}

bool DefaultIconEmoji::readyInDefaultState() {
	return true;
}

[[nodiscard]] int EditIconSize() {
	const auto tag = Data::CustomEmojiManager::SizeTag::Large;
	return Data::FrameSizeFromTag(tag) / style::DevicePixelRatio();
}

[[nodiscard]] int32 ChooseNextColorId(
		int32 currentId,
		std::vector<int32> &otherIds) {
	if (otherIds.size() == 1 && otherIds.front() == currentId) {
		otherIds = Data::ForumTopicColorIds();
	}
	const auto i = ranges::find(otherIds, currentId);
	if (i != end(otherIds)) {
		otherIds.erase(i);
	}
	return otherIds.empty()
		? currentId
		: otherIds[base::RandomIndex(otherIds.size())];
}

[[nodiscard]] not_null<Ui::AbstractButton*> EditIconButton(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<DefaultIcon> defaultIcon,
		rpl::producer<DocumentId> iconId,
		Fn<bool(not_null<Ui::RpWidget*>)> paintIconFrame) {
	using namespace Info::Profile;
	struct State {
		std::unique_ptr<Ui::Text::CustomEmoji> icon;
		QImage defaultIcon;
	};
	const auto tag = Data::CustomEmojiManager::SizeTag::Large;
	const auto size = EditIconSize();
	const auto result = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	result->show();
	const auto state = result->lifetime().make_state<State>();

	std::move(
		iconId
	) | rpl::start_with_next([=](DocumentId id) {
		const auto owner = &controller->session().data();
		state->icon = id
			? owner->customEmojiManager().create(
				id,
				[=] { result->update(); },
				tag)
			: nullptr;
		result->update();
	}, result->lifetime());

	std::move(
		defaultIcon
	) | rpl::start_with_next([=](DefaultIcon icon) {
		state->defaultIcon = Data::ForumTopicIconFrame(
			icon.colorId,
			icon.title,
			st::largeForumTopicIcon);
		result->update();
	}, result->lifetime());

	result->resize(size, size);
	result->paintRequest(
	) | rpl::filter([=] {
		return !paintIconFrame(result);
	}) | rpl::start_with_next([=](QRect clip) {
		auto args = Ui::Text::CustomEmoji::Context{
			.textColor = st::windowFg->c,
			.now = crl::now(),
			.paused = controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Layer),
		};
		auto p = QPainter(result);
		if (state->icon) {
			state->icon->paint(p, args);
		} else {
			const auto skip = (size - st::largeForumTopicIcon.size) / 2;
			p.drawImage(skip, skip, state->defaultIcon);
		}
	}, result->lifetime());

	return result;
}

[[nodiscard]] not_null<Ui::AbstractButton*> GeneralIconPreview(
		not_null<QWidget*> parent) {
	using namespace Info::Profile;
	struct State {
		QImage frame;
	};
	const auto size = EditIconSize();
	const auto result = Ui::CreateChild<Ui::AbstractButton>(parent.get());
	result->show();
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto state = result->lifetime().make_state<State>();

	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::start_with_next([=] {
		state->frame = Data::ForumTopicGeneralIconFrame(
			st::largeForumTopicIcon.size,
			st::windowSubTextFg->c);
		result->update();
	}, result->lifetime());

	result->resize(size, size);
	result->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(result);
		const auto skip = (size - st::largeForumTopicIcon.size) / 2;
		p.drawImage(skip, skip, state->frame);
	}, result->lifetime());

	return result;
}

struct IconSelector {
	Fn<bool(not_null<Ui::RpWidget*>)> paintIconFrame;
	rpl::producer<DocumentId> iconIdValue;
};

[[nodiscard]] IconSelector AddIconSelector(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::RpWidget*> button,
		not_null<Window::SessionController*> controller,
		rpl::producer<DefaultIcon> defaultIcon,
		rpl::producer<int> coverHeight,
		DocumentId iconId,
		Fn<void(object_ptr<Ui::RpWidget>)> placeFooter) {
	using namespace ChatHelpers;

	struct State {
		std::unique_ptr<Ui::EmojiFlyAnimation> animation;
		std::unique_ptr<HistoryView::StickerToast> toast;
		rpl::variable<DocumentId> iconId;
		QPointer<QWidget> button;
	};
	const auto state = box->lifetime().make_state<State>(State{
		.iconId = iconId,
		.button = button.get(),
	});

	const auto manager = &controller->session().data().customEmojiManager();

	auto factory = [=](DocumentId id, Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		const auto tag = Data::CustomEmojiManager::SizeTag::Large;
		if (id == kDefaultIconId) {
			return std::make_unique<DefaultIconEmoji>(
				rpl::duplicate(defaultIcon),
				std::move(repaint),
				tag);
		}
		return manager->create(id, std::move(repaint), tag);
	};

	const auto icons = &controller->session().data().forumIcons();
	const auto body = box->verticalLayout();
	const auto recent = [=] {
		auto list = icons->list();
		list.insert(begin(list), kDefaultIconId);
		return list;
	};
	const auto selector = body->add(
		object_ptr<EmojiListWidget>(body, EmojiListDescriptor{
			.show = controller->uiShow(),
			.mode = EmojiListWidget::Mode::TopicIcon,
			.paused = Window::PausedIn(controller, PauseReason::Layer),
			.customRecentList = DocumentListToRecent(recent()),
			.customRecentFactory = std::move(factory),
			.st = &st::reactPanelEmojiPan,
		}),
		st::reactPanelEmojiPan.padding);

	icons->requestDefaultIfUnknown();
	icons->defaultUpdates(
	) | rpl::start_with_next([=] {
		selector->provideRecent(DocumentListToRecent(recent()));
	}, selector->lifetime());

	placeFooter(selector->createFooter());

	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(box.get());
	shadow->show();

	rpl::combine(
		rpl::duplicate(coverHeight),
		selector->widthValue()
	) | rpl::start_with_next([=](int top, int width) {
		shadow->setGeometry(0, top, width, st::lineWidth);
	}, shadow->lifetime());

	selector->refreshEmoji();

	selector->scrollToRequests(
	) | rpl::start_with_next([=](int y) {
		box->scrollToY(y);
		shadow->update();
	}, selector->lifetime());

	rpl::combine(
		box->heightValue(),
		std::move(coverHeight),
		rpl::mappers::_1 - rpl::mappers::_2
	) | rpl::start_with_next([=](int height) {
		selector->setMinimalHeight(selector->width(), height);
	}, body->lifetime());

	const auto showToast = [=](not_null<DocumentData*> document) {
		if (!state->toast) {
			state->toast = std::make_unique<HistoryView::StickerToast>(
				controller,
				controller->widget()->bodyWidget(),
				[=] { state->toast = nullptr; });
		}
		state->toast->showFor(
			document,
			HistoryView::StickerToast::Section::TopicIcon);
	};

	selector->customChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		const auto owner = &controller->session().data();
		const auto document = data.document;
		const auto id = document->id;
		const auto custom = (id != kDefaultIconId);
		const auto premium = custom
			&& !ranges::contains(document->owner().forumIcons().list(), id);
		if (premium && !controller->session().premium()) {
			showToast(document);
			return;
		}
		const auto body = controller->window().widget()->bodyWidget();
		if (state->button && custom) {
			const auto &from = data.messageSendingFrom;
			auto args = Ui::ReactionFlyAnimationArgs{
				.id = { { id } },
				.flyIcon = from.frame,
				.flyFrom = body->mapFromGlobal(from.globalStartGeometry),
			};
			state->animation = std::make_unique<Ui::EmojiFlyAnimation>(
				body,
				&owner->reactions(),
				std::move(args),
				[=] { state->animation->repaint(); },
				[] { return st::windowFg->c; },
				Data::CustomEmojiSizeTag::Large);
		}
		state->iconId = id;
	}, selector->lifetime());

	auto paintIconFrame = [=](not_null<Ui::RpWidget*> button) {
		if (!state->animation) {
			return false;
		} else if (state->animation->paintBadgeFrame(button)) {
			return true;
		}
		InvokeQueued(state->animation->layer(), [=] {
			state->animation = nullptr;
		});
		return false;
	};
	return {
		.paintIconFrame = std::move(paintIconFrame),
		.iconIdValue = state->iconId.value(),
	};
}

} // namespace

void NewForumTopicBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<History*> forum) {
	EditForumTopicBox(box, controller, forum, MsgId(0));
}

void EditForumTopicBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		not_null<History*> forum,
		MsgId rootId) {
	const auto creating = !rootId;
	const auto topic = (!creating && forum->peer->forum())
		? forum->peer->forum()->topicFor(rootId)
		: nullptr;
	const auto created = topic && !topic->creating();
	box->setTitle(creating
		? tr::lng_forum_topic_new()
		: tr::lng_forum_topic_edit());

	box->setMaxHeight(st::editTopicMaxHeight);

	struct State {
		rpl::variable<DefaultIcon> defaultIcon;
		rpl::variable<DocumentId> iconId = 0;
		std::vector<int32> otherColorIds;
		mtpRequestId requestId = 0;
		Fn<bool(not_null<Ui::RpWidget*>)> paintIconFrame;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto &colors = Data::ForumTopicColorIds();
	state->iconId = topic ? topic->iconId() : 0;
	state->otherColorIds = colors;
	state->defaultIcon = DefaultIcon{
		topic ? topic->title() : QString(),
		topic ? topic->colorId() : ChooseNextColorId(0, state->otherColorIds)
	};

	const auto top = box->setPinnedToTopContent(
		object_ptr<Ui::VerticalLayout>(box));

	const auto title = top->add(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_forum_topic_title(),
			topic ? topic->title() : QString()),
		st::editTopicTitleMargin);
	box->setFocusCallback([=] {
		title->setFocusFast();
	});

	const auto paintIconFrame = [=](not_null<Ui::RpWidget*> widget) {
		return state->paintIconFrame && state->paintIconFrame(widget);
	};
	const auto icon = (topic && topic->isGeneral())
		? GeneralIconPreview(title->parentWidget())
		: EditIconButton(
			title->parentWidget(),
			controller,
			state->defaultIcon.value(),
			state->iconId.value(),
			paintIconFrame);

	title->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		icon->move(
			st::editTopicIconPosition.x(),
			st::editTopicIconPosition.y());
	}, icon->lifetime());

	state->iconId.value(
	) | rpl::start_with_next([=](DocumentId iconId) {
		icon->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			created || (iconId != 0));
	}, box->lifetime());

	icon->setClickedCallback([=] {
		const auto current = state->defaultIcon.current();
		state->defaultIcon = DefaultIcon{
			current.title,
			ChooseNextColorId(current.colorId, state->otherColorIds),
		};
	});
	title->changes(
	) | rpl::start_with_next([=] {
		state->defaultIcon = DefaultIcon{
			title->getLastText().trimmed(),
			state->defaultIcon.current().colorId,
		};
	}, title->lifetime());
	title->submits() | rpl::start_with_next([box] {
		box->triggerButton(0);
	}, title->lifetime());

	if (!topic || !topic->isGeneral()) {
		Ui::AddDividerText(top, tr::lng_forum_choose_title_and_icon());

		box->setScrollStyle(st::reactPanelScroll);

		auto selector = AddIconSelector(
			box,
			icon,
			controller,
			state->defaultIcon.value(),
			top->heightValue(),
			state->iconId.current(),
			[&](object_ptr<Ui::RpWidget> footer) {
				top->add(std::move(footer)); });
		state->paintIconFrame = std::move(selector.paintIconFrame);
		std::move(
			selector.iconIdValue
		) | rpl::start_with_next([=](DocumentId iconId) {
			state->iconId = (iconId != kDefaultIconId) ? iconId : 0;
		}, box->lifetime());
	}

	const auto create = [=] {
		const auto channel = forum->peer->asChannel();
		if (!channel || !channel->isForum()) {
			box->closeBox();
			return;
		} else if (title->getLastText().trimmed().isEmpty()) {
			title->showError();
			return;
		}
		using namespace HistoryView;
		controller->showSection(
			std::make_shared<ChatMemento>(ChatViewId{
				.history = forum,
				.repliesRootId = channel->forum()->reserveCreatingId(
					title->getLastText().trimmed(),
					state->defaultIcon.current().colorId,
					state->iconId.current()),
			}),
			Window::SectionShow::Way::ClearStack);
	};

	const auto save = [=] {
		const auto parent = forum->peer->forum();
		const auto topic = parent
			? parent->topicFor(rootId)
			: nullptr;
		if (!topic) {
			box->closeBox();
			return;
		} else if (state->requestId > 0) {
			return;
		} else if (title->getLastText().trimmed().isEmpty()) {
			title->showError();
			return;
		} else if (parent->creating(rootId)) {
			topic->applyTitle(title->getLastText().trimmed());
			topic->applyColorId(state->defaultIcon.current().colorId);
			topic->applyIconId(state->iconId.current());
			box->closeBox();
		} else {
			using Flag = MTPchannels_EditForumTopic::Flag;
			const auto api = &forum->session().api();
			const auto weak = base::make_weak(box);
			state->requestId = api->request(MTPchannels_EditForumTopic(
				MTP_flags(Flag::f_title
					| (topic->isGeneral() ? Flag() : Flag::f_icon_emoji_id)),
				topic->channel()->inputChannel,
				MTP_int(rootId),
				MTP_string(title->getLastText().trimmed()),
				MTP_long(state->iconId.current()),
				MTPBool(), // closed
				MTPBool() // hidden
			)).done([=](const MTPUpdates &result) {
				api->applyUpdates(result);
				if (const auto strong = weak.get()) {
					strong->closeBox();
				}
			}).fail([=](const MTP::Error &error) {
				if (const auto strong = weak.get()) {
					if (error.type() == u"TOPIC_NOT_MODIFIED") {
						strong->closeBox();
					} else {
						state->requestId = -1;
					}
				}
			}).send();
		}
	};

	if (creating) {
		box->addButton(tr::lng_create_group_create(), create);
	} else {
		box->addButton(tr::lng_settings_save(), save);
	}
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

std::unique_ptr<Ui::Text::CustomEmoji> MakeTopicIconEmoji(
		Data::TopicIconDescriptor descriptor,
		Fn<void()> repaint,
		Data::CustomEmojiSizeTag tag) {
	return std::make_unique<DefaultIconEmoji>(
		rpl::single(descriptor),
		std::move(repaint),
		tag);
}

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/business/settings_chat_intro.h"

#include "api/api_premium.h"
#include "boxes/peers/edit_peer_color_box.h" // ButtonStyleWithRightEmoji
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/application.h"
#include "data/business/data_business_info.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_sticker_player.h"
#include "history/view/history_view_about_view.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "settings/business/settings_recipients_helper.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

using namespace HistoryView;

class PreviewDelegate final : public DefaultElementDelegate {
public:
	PreviewDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	Context elementContext() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

class PreviewWrap final : public Ui::RpWidget {
public:
	PreviewWrap(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		rpl::producer<Data::ChatIntro> value);
	~PreviewWrap();

private:
	void paintEvent(QPaintEvent *e) override;

	void resizeTo(int width);
	void prepare(rpl::producer<Data::ChatIntro> value);

	const not_null<History*> _history;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;

	std::unique_ptr<AboutView> _view;
	QPoint _position;

};

class StickerPanel final {
public:
	StickerPanel();
	~StickerPanel();

	struct Descriptor {
		not_null<Window::SessionController*> controller;
		not_null<QWidget*> button;
	};
	void show(Descriptor &&descriptor);

	struct CustomChosen {
		not_null<DocumentData*> sticker;
	};
	[[nodiscard]] rpl::producer<CustomChosen> someCustomChosen() const {
		return _someCustomChosen.events();
	}

private:
	void create(const Descriptor &descriptor);

	base::unique_qptr<ChatHelpers::TabbedPanel> _panel;
	QPointer<QWidget> _panelButton;
	rpl::event_stream<CustomChosen> _someCustomChosen;

};

class ChatIntro final : public BusinessSection<ChatIntro> {
public:
	ChatIntro(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~ChatIntro();

	[[nodiscard]] bool closeByOutsideClick() const override;
	[[nodiscard]] rpl::producer<QString> title() override;

	void setInnerFocus() override {
		_setFocus();
	}

private:
	void setupContent(not_null<Window::SessionController*> controller);
	void save();

	Fn<void()> _setFocus;

	rpl::variable<Data::ChatIntro> _intro;

};

[[nodiscard]] int PartLimit(
		not_null<Main::Session*> session,
		const QString &key,
		int defaultValue) {
	return session->appConfig().get<int>(key, defaultValue);
}

[[nodiscard]] not_null<Ui::InputField*> AddPartInput(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> placeholder,
		QString current,
		int limit) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::settingsChatIntroField,
			std::move(placeholder),
			current),
		st::settingsChatIntroFieldMargins);
	field->setMaxLength(limit);
	Ui::AddLengthLimitLabel(field, limit);
	return field;
}

rpl::producer<std::shared_ptr<StickerPlayer>> IconPlayerValue(
		not_null<DocumentData*> sticker,
		Fn<void()> update) {
	const auto media = sticker->createMediaView();
	media->checkStickerLarge();
	media->goodThumbnailWanted();

	return rpl::single() | rpl::then(
		sticker->owner().session().downloaderTaskFinished()
	) | rpl::filter([=] {
		return media->loaded();
	}) | rpl::take(1) | rpl::map([=] {
		auto result = std::shared_ptr<StickerPlayer>();
		const auto info = sticker->sticker();
		const auto box = QSize(st::emojiSize, st::emojiSize);
		if (info->isLottie()) {
			result = std::make_shared<LottiePlayer>(
				ChatHelpers::LottiePlayerFromDocument(
					media.get(),
					ChatHelpers::StickerLottieSize::StickerEmojiSize,
					box,
					Lottie::Quality::High));
		} else if (info->isWebm()) {
			result = std::make_shared<WebmPlayer>(
				media->owner()->location(),
				media->bytes(),
				box);
		} else {
			result = std::make_shared<StaticStickerPlayer>(
				media->owner()->location(),
				media->bytes(),
				box);
		}
		result->setRepaintCallback(update);
		return result;
	});
}

[[nodiscard]] object_ptr<Ui::SettingsButton> CreateIntroStickerButton(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		rpl::producer<DocumentData*> stickerValue,
		Fn<void(DocumentData*)> stickerChosen) {
	const auto button = ButtonStyleWithRightEmoji(
		parent,
		tr::lng_chat_intro_random_sticker(tr::now),
		st::settingsButtonNoIcon);
	auto result = Settings::CreateButtonWithIcon(
		parent,
		tr::lng_chat_intro_choose_sticker(),
		*button.st);
	const auto raw = result.data();

	const auto right = Ui::CreateChild<Ui::RpWidget>(raw);
	right->show();

	struct State {
		StickerPanel panel;
		DocumentData *sticker = nullptr;
		std::shared_ptr<StickerPlayer> player;
		rpl::lifetime playerLifetime;
	};
	const auto state = right->lifetime().make_state<State>();
	state->panel.someCustomChosen(
	) | rpl::start_with_next([=](StickerPanel::CustomChosen chosen) {
		stickerChosen(chosen.sticker);
	}, raw->lifetime());

	std::move(
		stickerValue
	) | rpl::start_with_next([=](DocumentData *sticker) {
		state->sticker = sticker;
		if (sticker) {
			right->resize(button.emojiWidth + button.added, right->height());
			IconPlayerValue(
				sticker,
				[=] { right->update(); }
			) | rpl::start_with_next([=](
					std::shared_ptr<StickerPlayer> player) {
				state->player = std::move(player);
				right->update();
			}, state->playerLifetime);
		} else {
			state->playerLifetime.destroy();
			state->player = nullptr;
			right->resize(button.noneWidth + button.added, right->height());
			right->update();
		}
	}, right->lifetime());

	rpl::combine(
		raw->sizeValue(),
		right->widthValue()
	) | rpl::start_with_next([=](QSize outer, int width) {
		right->resize(width, outer.height());
		const auto skip = st::settingsButton.padding.right();
		right->moveToRight(skip - button.added, 0, outer.width());
	}, right->lifetime());

	right->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(right);
		const auto height = right->height();
		if (state->player) {
			if (state->player->ready()) {
				const auto frame = state->player->frame(
					QSize(st::emojiSize, st::emojiSize),
					QColor(0, 0, 0, 0),
					false,
					crl::now(),
					!right->window()->isActiveWindow()).image;
				const auto target = DownscaledSize(
					frame.size(),
					QSize(st::emojiSize, st::emojiSize));
				p.drawImage(
					QRect(
						button.added + (st::emojiSize - target.width()) / 2,
						(height - target.height()) / 2,
						target.width(),
						target.height()),
					frame);
				state->player->markFrameShown();
			}
		} else {
			const auto &font = st::normalFont;
			p.setFont(font);
			p.setPen(st::windowActiveTextFg);
			p.drawText(
				QPoint(
					button.added,
					(height - font->height) / 2 + font->ascent),
				tr::lng_chat_intro_random_sticker(tr::now));
		}
	}, right->lifetime());

	raw->setClickedCallback([=] {
		const auto controller = show->resolveWindow(
			ChatHelpers::WindowUsage::PremiumPromo);
		if (controller) {
			state->panel.show({
				.controller = controller,
				.button = right,
			});
		}
	});

	return result;
}

PreviewDelegate::PreviewDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	Fn<void()> update)
: _parent(parent)
, _pathGradient(MakePathShiftGradient(st, update)) {
}

bool PreviewDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto PreviewDelegate::elementPathShiftGradient()
-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

Context PreviewDelegate::elementContext() {
	return Context::History;
}

PreviewWrap::PreviewWrap(
	not_null<QWidget*> parent,
	not_null<Main::Session*> session,
	rpl::producer<Data::ChatIntro> value)
: RpWidget(parent)
, _history(session->data().history(session->userPeerId()))
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(
	_history->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	parent,
	_style.get(),
	[=] { update(); }))
, _position(0, st::msgMargin.bottom()) {
	_style->apply(_theme.get());

	session->data().viewRepaintRequest(
	) | rpl::start_with_next([=](not_null<const Element*> view) {
		if (view == _view->view()) {
			update();
		}
	}, lifetime());

	session->downloaderTaskFinished() | rpl::start_with_next([=] {
		update();
	}, lifetime());

	prepare(std::move(value));
}

PreviewWrap::~PreviewWrap() {
	_view = nullptr;
}

void PreviewWrap::prepare(rpl::producer<Data::ChatIntro> value) {
	_view = std::make_unique<AboutView>(
		_history.get(),
		_delegate.get());

	std::move(value) | rpl::start_with_next([=](Data::ChatIntro intro) {
		_view->make(std::move(intro), true);
		if (width() >= st::msgMinWidth) {
			resizeTo(width());
		}
		update();
	}, lifetime());

	widthValue(
	) | rpl::filter([=](int width) {
		return width >= st::msgMinWidth;
	}) | rpl::start_with_next([=](int width) {
		resizeTo(width);
	}, lifetime());
}

void PreviewWrap::resizeTo(int width) {
	const auto height = _position.y()
		+ _view->view()->resizeGetHeight(width)
		+ _position.y()
		+ st::msgServiceMargin.top()
		+ st::msgServiceGiftBoxTopSkip
		- st::msgServiceMargin.bottom();
	resize(width, height);
}

void PreviewWrap::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto clip = e->rect();
	if (!clip.isEmpty()) {
		p.setClipRect(clip);
		Window::SectionWidget::PaintBackground(
			p,
			_theme.get(),
			QSize(width(), window()->height()),
			clip);
	}

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		e->rect(),
		!window()->isActiveWindow());
	p.translate(_position);
	_view->view()->draw(p, context);
}

StickerPanel::StickerPanel() = default;

StickerPanel::~StickerPanel() = default;

void StickerPanel::show(Descriptor &&descriptor) {
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
	const auto parent = _panel->parentWidget();
	const auto global = button->mapToGlobal(QPoint());
	const auto local = parent->mapFromGlobal(global);
	_panel->moveBottomRight(
		local.y() + (st::normalFont->height / 2),
		local.x() + button->width() * 3);
	_panel->toggleAnimated();
}

void StickerPanel::create(const Descriptor &descriptor) {
	using Selector = ChatHelpers::TabbedSelector;
	using Descriptor = ChatHelpers::TabbedSelectorDescriptor;
	using Mode = ChatHelpers::TabbedSelector::Mode;
	const auto controller = descriptor.controller;
	const auto body = controller->window().widget()->bodyWidget();
	_panel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		body,
		controller,
		object_ptr<Selector>(
			nullptr,
			Descriptor{
				.show = controller->uiShow(),
				.st = st::backgroundEmojiPan,
				.level = Window::GifPauseReason::Layer,
				.mode = Mode::ChatIntro,
				.features = {
					.megagroupSet = false,
					.stickersSettings = false,
					.openStickerSets = false,
				},
			}));
	_panel->setDropDown(false);
	_panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	_panel->hide();

	_panel->selector()->fileChosen(
	) | rpl::start_with_next([=](ChatHelpers::FileChosen data) {
		_someCustomChosen.fire({ data.document });
		_panel->hideAnimated();
	}, _panel->lifetime());
}

ChatIntro::ChatIntro(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: BusinessSection(parent, controller) {
	setupContent(controller);
}

ChatIntro::~ChatIntro() {
	if (!Core::Quitting()) {
		save();
	}
}

bool ChatIntro::closeByOutsideClick() const {
	return false;
}

rpl::producer<QString> ChatIntro::title() {
	return tr::lng_chat_intro_title();
}

[[nodiscard]] rpl::producer<Data::ChatIntro> IntroWithRandomSticker(
		not_null<Main::Session*> session,
		rpl::producer<Data::ChatIntro> intro) {
	auto random = rpl::single(
		Api::RandomHelloStickerValue(session)
	) | rpl::then(rpl::duplicate(
		intro
	) | rpl::map([=](const Data::ChatIntro &intro) {
		return intro.sticker;
	}) | rpl::distinct_until_changed(
	) | rpl::filter([](DocumentData *sticker) {
		return !sticker;
	}) | rpl::map([=] {
		return Api::RandomHelloStickerValue(session);
	})) | rpl::flatten_latest();

	return rpl::combine(
		std::move(intro),
		std::move(random)
	) | rpl::map([=](Data::ChatIntro intro, DocumentData *hello) {
		if (!intro.sticker) {
			intro.sticker = hello;
		}
		return intro;
	});
}

void ChatIntro::setupContent(
		not_null<Window::SessionController*> controller) {
	using namespace rpl::mappers;

	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	const auto session = &controller->session();
	_intro = controller->session().user()->businessDetails().intro;

	const auto change = [=](Fn<void(Data::ChatIntro &)> modify) {
		auto intro = _intro.current();
		modify(intro);
		_intro = intro;
	};

	content->add(
		object_ptr<PreviewWrap>(
			content,
			session,
			IntroWithRandomSticker(session, _intro.value())),
		{});

	const auto title = AddPartInput(
		content,
		tr::lng_chat_intro_enter_title(),
		_intro.current().title,
		PartLimit(session, u"intro_title_length_limit"_q, 32));
	const auto description = AddPartInput(
		content,
		tr::lng_chat_intro_enter_message(),
		_intro.current().description,
		PartLimit(session, u"intro_description_length_limit"_q, 70));
	content->add(CreateIntroStickerButton(
		content,
		controller->uiShow(),
		_intro.value() | rpl::map([](const Data::ChatIntro &intro) {
			return intro.sticker;
		}) | rpl::distinct_until_changed(),
		[=](DocumentData *sticker) {
			change([&](Data::ChatIntro &intro) {
				intro.sticker = sticker;
			});
		}));
	Ui::AddSkip(content);

	title->changes() | rpl::start_with_next([=] {
		change([&](Data::ChatIntro &intro) {
			intro.title = title->getLastText();
		});
	}, title->lifetime());

	description->changes() | rpl::start_with_next([=] {
		change([&](Data::ChatIntro &intro) {
			intro.description = description->getLastText();
		});
	}, description->lifetime());

	_setFocus = [=] {
		title->setFocusFast();
	};

	Ui::AddDividerText(
		content,
		tr::lng_chat_intro_about(),
		st::peerAppearanceDividerTextMargin);
	Ui::AddSkip(content);

	const auto resetWrap = content->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			content,
			object_ptr<Ui::SettingsButton>(
				content,
				tr::lng_chat_intro_reset(),
				st::settingsAttentionButton
			)));
	resetWrap->toggleOn(
		_intro.value() | rpl::map([](const Data::ChatIntro &intro) {
			return !!intro;
		}));
	resetWrap->entity()->setClickedCallback([=] {
		_intro = Data::ChatIntro();
		title->clear();
		description->clear();
		title->setFocus();
	});

	Ui::ResizeFitChild(this, content);
}

void ChatIntro::save() {
	const auto show = controller()->uiShow();
	const auto fail = [=](QString error) {
	};
	controller()->session().data().businessInfo().saveChatIntro(
		_intro.current(),
		fail);
}

} // namespace

Type ChatIntroId() {
	return ChatIntro::Id();
}

} // namespace Settings

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_widget.h"

#include "api/api_peer_photo.h"
#include "apiwrap.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/stickers_list_widget.h"
#include "data/data_document.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "editor/photo_editor_layer_widget.h" // Editor::kProfilePhotoSize.
#include "info/userpic/info_userpic_bubble_wrap.h"
#include "info/userpic/info_userpic_color_circle_button.h"
#include "info/userpic/info_userpic_colors_editor.h"
#include "info/userpic/info_userpic_emoji_builder_common.h"
#include "info/userpic/info_userpic_emoji_builder_preview.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/emoji_button.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info_userpic_builder.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_menu_icons.h"

namespace UserpicBuilder {
namespace {

void AlignChildren(not_null<Ui::RpWidget*> widget, int fullWidth) {
	const auto children = widget->children();
	const auto widgets = ranges::views::all(
		children
	) | ranges::views::filter([](not_null<const QObject*> object) {
		return object->isWidgetType();
	}) | ranges::views::transform([](not_null<QObject*> object) {
		return static_cast<QWidget*>(object.get());
	}) | ranges::to_vector;

	const auto widgetWidth = widgets.front()->width();
	const auto widgetsCount = widgets.size();
	const auto widgetsWidth = widgetWidth * widgetsCount;
	const auto step = (fullWidth - widgetsWidth) / (widgetsCount - 1);
	for (auto i = 0; i < widgetsCount; i++) {
		widgets[i]->move(i * (widgetWidth + step), widgets[i]->y());
	}
}

[[nodiscard]] QImage GenerateSpecial(
		int size,
		const std::vector<QColor> colors) {
	if (colors.empty()) {
		auto image = QImage(
			Size(size * style::DevicePixelRatio()),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(style::DevicePixelRatio());
		image.fill(Qt::transparent);
		{
			auto p = QPainter(&image);
			st::userpicBuilderEmojiColorPlus.icon.paintInCenter(
				p,
				Rect(Size(size)));
		}
		return image;
	} else {
		auto image = GenerateGradient(Size(size), colors);
		{
			auto p = QPainter(&image);
			constexpr auto kEllipseSize = 1;
			const auto center = QPointF(size / 2., size / 2.);
			const auto shift = QPointF(kEllipseSize * 4, 0);
			p.setPen(Qt::NoPen);
			p.setBrush(st::boxBg);
			p.drawEllipse(center, kEllipseSize, kEllipseSize);
			p.drawEllipse(center + shift, kEllipseSize, kEllipseSize);
			p.drawEllipse(center - shift, kEllipseSize, kEllipseSize);
		}
		return image;
	}
}

[[nodiscard]] std::vector<std::vector<QColor>> PaletteGradients() {
	auto v = std::vector<std::vector<QColor>>{
		{
			QColor(32, 226, 205),
			QColor(14, 225, 241),
			QColor(77, 141, 255),
			QColor(43, 191, 255),
		},
		{
			QColor(69, 247, 183),
			QColor(31, 241, 217),
			QColor(94, 182, 251),
			QColor(31, 206, 235),
		},
		{
			QColor(193, 229, 38),
			QColor(128, 223, 43),
			QColor(9, 210, 96),
			QColor(94, 220, 64),
		},
		{
			QColor(255, 212, 18),
			QColor(255, 167, 67),
			QColor(245, 105, 78),
			QColor(245, 119, 44),
		},
		{
			QColor(246, 167, 48),
			QColor(255, 119, 66),
			QColor(246, 72, 132),
			QColor(239, 91, 65),
		},
		{
			QColor(255, 178, 58),
			QColor(254, 126, 98),
			QColor(249, 75, 160),
			QColor(251, 92, 128),
		},
		{
			QColor(255, 114, 169),
			QColor(226, 105, 255),
			QColor(131, 124, 255),
			QColor(176, 99, 255),
		},
	};
	for (auto &g : v) {
		// Rotate 180 degrees.
		std::swap(g[0], g[2]);
		std::swap(g[1], g[3]);
	}
	return v;
}

void ShowGradientEditor(
		not_null<Window::SessionController*> controller,
		StartData data,
		Fn<void(std::vector<QColor>)> &&doneCallback) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			rpl::event_stream<> saveRequests;
		};
		const auto state = box->lifetime().make_state<State>();
		box->setTitle(tr::lng_chat_theme_change());
		box->addButton(tr::lng_settings_save(), [=] {
			state->saveRequests.fire({});
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

		auto content = CreateGradientEditor(
			box,
			(data.documentId
				? controller->session().data().document(
					data.documentId).get()
				: nullptr),
			data.gradientEditorColors,
			BothWayCommunication<std::vector<QColor>>{
				state->saveRequests.events(),
				[=](std::vector<QColor> colors) {
					box->closeBox();
					doneCallback(std::move(colors));
				},
			});
		box->setWidth(content->width());
		box->addRow(std::move(content), {});
	}));
}

class EmojiSelector final : public Ui::RpWidget {
public:
	EmojiSelector(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		rpl::producer<std::vector<DocumentId>> recent);

	[[nodiscard]] rpl::producer<not_null<DocumentData*>> chosen() const;

private:
	using Footer = ChatHelpers::TabbedSelector::InnerFooter;
	using List = ChatHelpers::TabbedSelector::Inner;
	using Type = ChatHelpers::SelectorTab;
	void createSelector(Type type);

	struct Selector {
		not_null<List*> list;
		not_null<Footer*> footer;
	};
	[[nodiscard]] Selector createEmojiList(
		not_null<Ui::ScrollArea*> scroll);
	[[nodiscard]] Selector createStickersList(
		not_null<Ui::ScrollArea*> scroll) const;

	const not_null<Window::SessionController*> _controller;
	base::unique_qptr<Ui::RpWidget> _container;

	rpl::event_stream<> _recentChanges;
	std::vector<DocumentId> _lastRecent;
	rpl::event_stream<not_null<DocumentData*>> _chosen;

};

EmojiSelector::EmojiSelector(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	rpl::producer<std::vector<DocumentId>> recent)
: RpWidget(parent)
, _controller(controller) {
	std::move(
		recent
	) | rpl::start_with_next([=](std::vector<DocumentId> ids) {
		_lastRecent = std::move(ids);
		_recentChanges.fire({});
	}, lifetime());
	createSelector(Type::Emoji);
}

rpl::producer<not_null<DocumentData*>> EmojiSelector::chosen() const {
	return _chosen.events();
}

EmojiSelector::Selector EmojiSelector::createEmojiList(
		not_null<Ui::ScrollArea*> scroll) {
	const auto session = &_controller->session();
	const auto manager = &session->data().customEmojiManager();
	const auto tag = Data::CustomEmojiManager::SizeTag::Large;
	auto args = ChatHelpers::EmojiListDescriptor{
		.show = _controller->uiShow(),
		.mode = ChatHelpers::EmojiListMode::UserpicBuilder,
		.paused = [=] { return true; },
		.customRecentList = _lastRecent,
		.customRecentFactory = [=](DocumentId id, Fn<void()> repaint) {
			return manager->create(id, std::move(repaint), tag);
		},
		.st = &st::userpicBuilderEmojiPan,
	};
	const auto list = scroll->setOwnedWidget(
		object_ptr<ChatHelpers::EmojiListWidget>(scroll, std::move(args)));
	const auto footer = list->createFooter().data();
	list->refreshEmoji();
	list->customChosen(
	) | rpl::start_with_next([=](const ChatHelpers::FileChosen &chosen) {
		_chosen.fire_copy(chosen.document);
	}, list->lifetime());
	_recentChanges.events(
	) | rpl::start_with_next([=] {
		createSelector(Type::Emoji);
	}, list->lifetime());
	list->setAllowWithoutPremium(true);
	return { list, footer };
}

EmojiSelector::Selector EmojiSelector::createStickersList(
		not_null<Ui::ScrollArea*> scroll) const {
	const auto list = scroll->setOwnedWidget(
		object_ptr<ChatHelpers::StickersListWidget>(
			scroll,
			_controller,
			Window::GifPauseReason::Any,
			ChatHelpers::StickersListMode::UserpicBuilder));
	const auto footer = list->createFooter().data();
	list->refreshRecent();
	list->chosen(
	) | rpl::start_with_next([=](const ChatHelpers::FileChosen &chosen) {
		_chosen.fire_copy(chosen.document);
	}, list->lifetime());
	return { list, footer };
}

void EmojiSelector::createSelector(Type type) {
	Expects((type == Type::Emoji) || (type == Type::Stickers));

	const auto isEmoji = (type == Type::Emoji);
	const auto &stScroll = st::reactPanelScroll;

	_container = base::make_unique_q<Ui::RpWidget>(this);
	const auto container = _container.get();
	container->show();
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		container->setGeometry(Rect(s));
	}, container->lifetime());

	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(container, stScroll);

	const auto selector = isEmoji
		? createEmojiList(scroll)
		: createStickersList(scroll);
	selector.footer->setParent(container);

	const auto toggleButton = Ui::CreateChild<Ui::AbstractButton>(container);
	const auto &togglePos = st::userpicBuilderEmojiSelectorTogglePosition;
	{
		const auto &pos = togglePos;
		toggleButton->resize(st::menuIconStickers.size()
			// Trying to overlap the settings button under.
			+ QSize(pos.x() * 2, pos.y() * 2));
		toggleButton->show();
		toggleButton->paintRequest(
		) | rpl::start_with_next([=] {
			auto p = QPainter(toggleButton);
			const auto r = toggleButton->rect()
				- QMargins(pos.x(), pos.y(), pos.x(), pos.y());
			p.fillRect(r, st::boxBg);
			if (isEmoji) {
				st::userpicBuilderEmojiToggleStickersIcon.paintInCenter(p, r);
			} else {
				st::defaultEmojiPan.icons.people.paintInCenter(p, r);
			}
		}, toggleButton->lifetime());
	}
	toggleButton->show();
	toggleButton->setClickedCallback([=] {
		createSelector(isEmoji ? Type::Stickers : Type::Emoji);
	});

	rpl::combine(
		scroll->scrollTopValue(),
		scroll->heightValue()
	) | rpl::start_with_next([=](int scrollTop, int scrollHeight) {
		const auto scrollBottom = scrollTop + scrollHeight;
		selector.list->setVisibleTopBottom(scrollTop, scrollBottom);
	}, selector.list->lifetime());

	selector.list->scrollToRequests(
	) | rpl::start_with_next([=](int y) {
		scroll->scrollToY(y);
		// _shadow->update();
	}, selector.list->lifetime());

	const auto separator = Ui::CreateChild<Ui::RpWidget>(container);
	separator->paintRequest(
	) | rpl::start_with_next([=](const QRect &r) {
		auto p = QPainter(separator);
		p.fillRect(r, st::shadowFg);
	}, separator->lifetime());

	selector.footer->show();
	separator->show();
	scroll->show();

	const auto scrollWidth = stScroll.width;
	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto left = st::userpicBuilderEmojiSelectorLeft;
		const auto mostTop = st::userpicBuilderEmojiSelectorLeft;

		toggleButton->move(QPoint(left, mostTop));

		selector.footer->setGeometry(
			(isEmoji ? (rect::right(toggleButton) - togglePos.x()) : left),
			mostTop,
			s.width() - left,
			selector.footer->height());

		separator->setGeometry(
			0,
			rect::bottom(selector.footer),
			s.width(),
			st::lineWidth);

		const auto listWidth = s.width() - st::boxRadius * 2;
		selector.list->resizeToWidth(listWidth);
		scroll->setGeometry(
			st::boxRadius,
			rect::bottom(separator),
			selector.list->width() + scrollWidth,
			s.height() - rect::bottom(separator));
		selector.list->setMinimalHeight(listWidth, scroll->height());
	}, lifetime());

	// Reset all animations.
	selector.list->hideFinished();
}

} // namespace

not_null<Ui::VerticalLayout*> CreateUserpicBuilder(
		not_null<Ui::RpWidget*> parent,
		not_null<Window::SessionController*> controller,
		StartData data,
		BothWayCommunication<UserpicBuilder::Result> communication) {
	const auto container = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	struct State {
		std::vector<not_null<CircleButton*>> circleButtons;
		Ui::Animations::Simple chosenColorAnimation;
		int colorIndex = -1;

		std::vector<QColor> editorColors;
		StartData gradientEditorStartData;
	};
	const auto state = container->lifetime().make_state<State>();

	const auto preview = container->add(
		object_ptr<Ui::CenterWrap<EmojiUserpic>>(
			container,
			object_ptr<EmojiUserpic>(
				container,
				Size(st::settingsInfoPhotoSize),
				data.isForum)),
		st::userpicBuilderEmojiPreviewPadding)->entity();
	if (const auto id = data.documentId) {
		const auto document = controller->session().data().document(id);
		if (document && document->sticker()) {
			preview->setDocument(document);
		}
	}

	container->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_userpic_builder_color_subtitle(),
				st::userpicBuilderEmojiSubtitle)),
		st::userpicBuilderEmojiSubtitlePadding);

	const auto paletteBg = Ui::AddBubbleWrap(
		container,
		QSize(
			st::userpicBuilderEmojiBubblePaletteWidth,
			std::abs(Ui::BubbleWrapInnerRect(QRect(0, 0, 0, 0)).height())
				+ st::userpicBuilderEmojiAccentColorSize
				+ rect::m::sum::v(
					st::userpicBuilderEmojiBubblePalettePadding)));
	const auto palette = Ui::CreateChild<Ui::VerticalLayout>(paletteBg.get());
	{
		constexpr auto kColorsCount = int(7);
		const auto checkIsSpecial = [=](int i) {
			return (i == kColorsCount);
		};
		const auto size = st::userpicBuilderEmojiAccentColorSize;
		const auto paletteGradients = PaletteGradients();
		for (auto i = 0; i < kColorsCount + 1; i++) {
			const auto isSpecial = checkIsSpecial(i);
			const auto colors = paletteGradients[i % kColorsCount];
			const auto button = Ui::CreateChild<CircleButton>(palette);
			state->circleButtons.push_back(button);
			button->resize(size, size);
			button->setBrush(isSpecial
				? GenerateSpecial(size, state->editorColors)
				: GenerateGradient(Size(size), colors));

			const auto openEditor = isSpecial
				? Fn<void()>([=] {
					if (checkIsSpecial(state->colorIndex)) {
						state->colorIndex = -1;
					}
					ShowGradientEditor(
						controller,
						state->gradientEditorStartData,
						[=](std::vector<QColor> colors) {
							state->editorColors = std::move(colors);
							button->setBrush(
								GenerateSpecial(size, state->editorColors));
							button->clicked({}, Qt::LeftButton);
						});
				})
				: nullptr;

			button->setClickedCallback([=] {
				if (openEditor && state->editorColors.empty()) {
					return openEditor();
				}
				const auto was = state->colorIndex;
				const auto now = i;
				if (was == now) {
					if (openEditor) {
						openEditor();
					}
					return;
				}
				state->chosenColorAnimation.stop();
				state->chosenColorAnimation.start([=](float64 progress) {
					if (was >= 0) {
						state->circleButtons[was]->setSelectedProgress(
							1. - progress);
					}
					state->circleButtons[now]->setSelectedProgress(progress);
				}, 0., 1., st::universalDuration);
				state->colorIndex = now;

				const auto result = isSpecial
					? state->editorColors
					: colors;
				state->gradientEditorStartData.gradientEditorColors = result;
				preview->setGradientColors(result);
			});
		}
		const auto current = data.builderColorIndex % kColorsCount;
		state->circleButtons[current]->setSelectedProgress(1.);
		state->circleButtons[current]->clicked({}, Qt::LeftButton);
	}
	paletteBg->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		palette->setGeometry(Ui::BubbleWrapInnerRect(Rect(s))
			- st::userpicBuilderEmojiBubblePalettePadding);
		AlignChildren(palette, palette->width());
	}, palette->lifetime());

	container->add(
		object_ptr<Ui::CenterWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_userpic_builder_emoji_subtitle(),
				st::userpicBuilderEmojiSubtitle)),
		st::userpicBuilderEmojiSubtitlePadding);

	const auto selectorBg = Ui::AddBubbleWrap(
		container,
		QSize(
			st::userpicBuilderEmojiBubblePaletteWidth,
			st::userpicBuilderEmojiSelectorMinHeight));
	const auto selector = Ui::CreateChild<EmojiSelector>(
		selectorBg.get(),
		controller,
		base::take(data.documents));
	selector->chosen(
	) | rpl::start_with_next([=](not_null<DocumentData*> document) {
		state->gradientEditorStartData.documentId = document->id;
		preview->setDocument(document);
	}, preview->lifetime());
	selectorBg->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		selector->setGeometry(Ui::BubbleWrapInnerRect(Rect(s)));
	}, selector->lifetime());

	base::take(
		communication.triggers
	) | rpl::start_with_next([=, done = base::take(communication.result)] {
		preview->result(Editor::kProfilePhotoSize, [=](Result result) {
			done(std::move(result));
		});
	}, preview->lifetime());

	return container;
}

not_null<Ui::RpWidget*> CreateEmojiUserpic(
		not_null<Ui::RpWidget*> parent,
		const QSize &size,
		rpl::producer<not_null<DocumentData*>> document,
		rpl::producer<int> colorIndex,
		bool isForum) {
	const auto paletteGradients = PaletteGradients();
	const auto widget = Ui::CreateChild<EmojiUserpic>(
		parent.get(),
		size,
		isForum);
	std::move(
		document
	) | rpl::start_with_next([=](not_null<DocumentData*> d) {
		widget->setDocument(d);
	}, widget->lifetime());
	std::move(
		colorIndex
	) | rpl::start_with_next([=](int index) {
		widget->setGradientColors(
			paletteGradients[index % paletteGradients.size()]);
	}, widget->lifetime());
	return widget;
}

} // namespace UserpicBuilder

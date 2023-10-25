/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_reply_options.h"

#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace HistoryView::Controls {
namespace {

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

[[nodiscard]] std::unique_ptr<Ui::ChatTheme> DefaultThemeOn(
		rpl::lifetime &lifetime) {
	auto result = std::make_unique<Ui::ChatTheme>();

	using namespace Window::Theme;
	const auto push = [=, raw = result.get()] {
		const auto background = Background();
		const auto &paper = background->paper();
		raw->setBackground({
			.prepared = background->prepared(),
			.preparedForTiled = background->preparedForTiled(),
			.gradientForFill = background->gradientForFill(),
			.colorForFill = background->colorForFill(),
			.colors = paper.backgroundColors(),
			.patternOpacity = paper.patternOpacity(),
			.gradientRotation = paper.gradientRotation(),
			.isPattern = paper.isPattern(),
			.tile = background->tile(),
		});
	};

	push();
	Background()->updates(
	) | rpl::start_with_next([=](const BackgroundUpdate &update) {
		if (update.type == BackgroundUpdate::Type::New
			|| update.type == BackgroundUpdate::Type::Changed) {
			push();
		}
	}, lifetime);

	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> AddQuoteTracker(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<HistoryItem*> item,
		const TextWithEntities &quote) {
	struct State {
		std::unique_ptr<Ui::ChatTheme> theme;
		std::unique_ptr<Ui::ChatStyle> style;
		std::unique_ptr<PreviewDelegate> delegate;
		std::unique_ptr<Element> element;
		rpl::variable<TextSelection> selection;
		Ui::PeerUserpicView userpic;
		QPoint position;

		base::Timer trippleClickTimer;
		TextSelectType selectType = TextSelectType::Letters;
		uint16 symbol = 0;
		bool afterSymbol = false;
		bool textCursor = false;
		bool selecting = false;
		bool over = false;
		uint16 selectionStartSymbol = 0;
		bool selectionStartAfterSymbol = false;
	};

	const auto preview = box->addRow(object_ptr<Ui::RpWidget>(box), {});
	const auto state = preview->lifetime().make_state<State>();
	state->theme = DefaultThemeOn(preview->lifetime());

	state->style = std::make_unique<Ui::ChatStyle>();
	state->style->apply(state->theme.get());

	state->delegate = std::make_unique<PreviewDelegate>(
		box,
		state->style.get(),
		[=] { preview->update(); });

	state->element = item->createView(state->delegate.get());
	state->element->initDimensions();
	state->position = QPoint(0, st::msgMargin.bottom());

	state->selection = state->element->selectionFromQuote(quote);

	const auto session = &show->session();
	session->data().viewRepaintRequest(
	) | rpl::start_with_next([=](not_null<const Element*> view) {
		if (view == state->element.get()) {
			preview->update();
		}
	}, preview->lifetime());

	state->selection.changes() | rpl::start_with_next([=] {
		preview->update();
	}, preview->lifetime());

	const auto resolveNewSelection = [=] {
		const auto make = [](uint16 symbol, bool afterSymbol) {
			return uint16(symbol + (afterSymbol ? 1 : 0));
		};
		const auto first = make(state->symbol, state->afterSymbol);
		const auto second = make(
			state->selectionStartSymbol,
			state->selectionStartAfterSymbol);
		const auto result = (first <= second)
			? TextSelection{ first, second }
			: TextSelection{ second, first };
		return state->element->adjustSelection(result, state->selectType);
	};
	const auto startSelection = [=](TextSelectType type) {
		if (state->selecting && state->selectType >= type) {
			return;
		}
		state->selecting = true;
		state->selectType = type;
		state->selectionStartSymbol = state->symbol;
		state->selectionStartAfterSymbol = state->afterSymbol;
		if (!state->textCursor) {
			preview->setCursor(style::cur_text);
		}
		preview->update();
	};
	const auto media = item->media();
	const auto onlyMessageText = media
		&& (media->webpage()
			|| media->game()
			|| (!media->photo() && !media->document()));
	preview->setMouseTracking(true);
	preview->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		const auto mouse = static_cast<QMouseEvent*>(e.get());
		if (type == QEvent::MouseMove) {
			auto request = StateRequest{
				.flags = Ui::Text::StateRequest::Flag::LookupSymbol,
				.onlyMessageText = onlyMessageText,
			};
			auto resolved = state->element->textState(
				mouse->pos() - state->position,
				request);
			state->over = true;
			const auto text = (resolved.cursor == CursorState::Text);
			if (state->textCursor != text) {
				state->textCursor = text;
				preview->setCursor((text || state->selecting)
					? style::cur_text
					: style::cur_default);
			}
			if (state->symbol != resolved.symbol
				|| state->afterSymbol != resolved.afterSymbol) {
				state->symbol = resolved.symbol;
				state->afterSymbol = resolved.afterSymbol;
				if (state->selecting) {
					preview->update();
				}
			}
		} else if (type == QEvent::Leave && state->over) {
			state->over = false;
			if (state->textCursor) {
				state->textCursor = false;
				if (!state->selecting) {
					preview->setCursor(style::cur_default);
				}
			}
		} else if (type == QEvent::MouseButtonDblClick && state->over) {
			startSelection(TextSelectType::Words);
			state->trippleClickTimer.callOnce(
				QApplication::doubleClickInterval());
		} else if (type == QEvent::MouseButtonPress && state->over) {
			startSelection(state->trippleClickTimer.isActive()
				? TextSelectType::Paragraphs
				: TextSelectType::Letters);
		} else if (type == QEvent::MouseButtonRelease && state->selecting) {
			const auto result = resolveNewSelection();
			state->selecting = false;
			state->selectType = TextSelectType::Letters;
			state->selection = result;
			if (!state->textCursor) {
				preview->setCursor(style::cur_default);
			}
		}
	}, preview->lifetime());

	preview->widthValue(
	) | rpl::filter([=](int width) {
		return width > st::msgMinWidth;
	}) | rpl::start_with_next([=](int width) {
		const auto height = state->element->resizeGetHeight(width)
			+ state->position.y()
			+ st::msgMargin.top();
		preview->resize(width, height);
	}, preview->lifetime());

	box->setAttribute(Qt::WA_OpaquePaintEvent, false);
	box->paintRequest() | rpl::start_with_next([=](QRect clip) {
		Window::SectionWidget::PaintBackground(
			state->theme.get(),
			box,
			box->window()->height(),
			0,
			clip);
	}, box->lifetime());

	preview->paintRequest() | rpl::start_with_next([=](QRect clip) {
		auto p = Painter(preview);
		auto hq = PainterHighQualityEnabler(p);
		p.translate(state->position);
		auto context = state->theme->preparePaintContext(
			state->style.get(),
			preview->rect(),
			clip,
			!box->window()->isActiveWindow());
		context.outbg = state->element->hasOutLayout();
		context.selection = state->selecting
			? resolveNewSelection()
			: state->selection.current();
		state->element->draw(p, context);
		if (state->element->displayFromPhoto()) {
			auto userpicMinBottomSkip = st::historyPaddingBottom
				+ st::msgMargin.bottom();
			auto userpicBottom = preview->height()
				- state->element->marginBottom()
				- state->element->marginTop();
			const auto userpicTop = userpicBottom - st::msgPhotoSize;
			if (const auto from = item->displayFrom()) {
				from->paintUserpicLeft(
					p,
					state->userpic,
					st::historyPhotoLeft,
					userpicTop,
					preview->width(),
					st::msgPhotoSize);
			} else if (const auto info = item->hiddenSenderInfo()) {
				if (info->customUserpic.empty()) {
					info->emptyUserpic.paintCircle(
						p,
						st::historyPhotoLeft,
						userpicTop,
						preview->width(),
						st::msgPhotoSize);
				} else {
					const auto valid = info->paintCustomUserpic(
						p,
						state->userpic,
						st::historyPhotoLeft,
						userpicTop,
						preview->width(),
						st::msgPhotoSize);
					if (!valid) {
						info->customUserpic.load(session, item->fullId());
					}
				}
			} else {
				Unexpected("Corrupt forwarded information in message.");
			}
		}
	}, preview->lifetime());

	return state->selection.value(
	) | rpl::map([=](TextSelection selection) {
		return state->element->selectedQuote(selection);
	});
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

} // namespace

void ShowReplyToChatBox(
		std::shared_ptr<ChatHelpers::Show> show,
		FullReplyTo reply,
		Fn<void()> clearOldDraft) {
	class Controller final : public ChooseRecipientBoxController {
	public:
		using Chosen = not_null<Data::Thread*>;

		Controller(not_null<Main::Session*> session)
		: ChooseRecipientBoxController(
			session,
			[=](Chosen thread) mutable { _singleChosen.fire_copy(thread); },
			nullptr) {
		}

		void rowClicked(not_null<PeerListRow*> row) override final {
			ChooseRecipientBoxController::rowClicked(row);
		}

		[[nodiscard]] rpl::producer<Chosen> singleChosen() const{
			return _singleChosen.events();
		}

		bool respectSavedMessagesChat() const override {
			return false;
		}

	private:
		void prepareViewHook() override {
			delegate()->peerListSetTitle(tr::lng_reply_in_another_title());
		}

		rpl::event_stream<Chosen> _singleChosen;

	};

	struct State {
		not_null<PeerListBox*> box;
		not_null<Controller*> controller;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto session = &show->session();
	const auto state = [&] {
		auto controller = std::make_unique<Controller>(session);
		const auto controllerRaw = controller.get();
		auto box = Box<PeerListBox>(std::move(controller), [=](
				not_null<PeerListBox*> box) {
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		});
		const auto boxRaw = box.data();
		show->show(std::move(box));
		auto state = State{ boxRaw, controllerRaw };
		return boxRaw->lifetime().make_state<State>(std::move(state));
	}();

	auto chosen = [=](not_null<Data::Thread*> thread) mutable {
		const auto history = thread->owningHistory();
		const auto topicRootId = thread->topicRootId();
		const auto draft = history->localDraft(topicRootId);
		const auto textWithTags = draft
			? draft->textWithTags
			: TextWithTags();
		const auto cursor = draft ? draft->cursor : MessageCursor();
		reply.topicRootId = topicRootId;
		history->setLocalDraft(std::make_unique<Data::Draft>(
			textWithTags,
			reply,
			cursor,
			Data::WebPageDraft()));
		history->clearLocalEditDraft(topicRootId);
		history->session().changes().entryUpdated(
			thread,
			Data::EntryUpdate::Flag::LocalDraftSet);

		if (clearOldDraft) {
			crl::on_main(&history->session(), clearOldDraft);
		}
		return true;
	};
	auto callback = [=, chosen = std::move(chosen)](
			Controller::Chosen thread) mutable {
		const auto weak = Ui::MakeWeak(state->box);
		if (!chosen(thread)) {
			return;
		} else if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	};
	state->controller->singleChosen(
	) | rpl::start_with_next(std::move(callback), state->box->lifetime());
}

void EditReplyOptions(
		std::shared_ptr<ChatHelpers::Show> show,
		FullReplyTo reply,
		Fn<void(FullReplyTo)> done,
		Fn<void()> highlight,
		Fn<void()> clearOldDraft) {
	const auto session = &show->session();
	const auto item = session->data().message(reply.messageId);
	if (!item) {
		return;
	}
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);

		const auto bottom = box->setPinnedToBottomContent(
			object_ptr<Ui::VerticalLayout>(box));
		const auto addSkip = [&] {
			const auto skip = bottom->add(object_ptr<Ui::FixedHeightWidget>(
				bottom,
				st::settingsPrivacySkipTop));
			skip->paintRequest() | rpl::start_with_next([=](QRect clip) {
				QPainter(skip).fillRect(clip, st::boxBg);
			}, skip->lifetime());
		};

		addSkip();

		Settings::AddButton(
			bottom,
			tr::lng_reply_in_another_chat(),
			st::settingsButton,
			{ &st::menuIconReplace }
		)->setClickedCallback([=] {
			ShowReplyToChatBox(show, reply, clearOldDraft);
		});

		Settings::AddButton(
			bottom,
			tr::lng_reply_show_in_chat(),
			st::settingsButton,
			{ &st::menuIconShowInChat }
		)->setClickedCallback(highlight);

		const auto finish = [=](FullReplyTo result) {
			const auto weak = Ui::MakeWeak(box);
			done(std::move(result));
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
		};

		Settings::AddButton(
			bottom,
			tr::lng_reply_remove(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconDeleteAttention }
		)->setClickedCallback([=] {
			finish({});
		});

		if (!item->originalText().empty()) {
			addSkip();
			Settings::AddDividerText(
				bottom,
				tr::lng_reply_about_quote());
		}

		struct State {
			rpl::variable<TextWithEntities> quote;
		};
		const auto state = box->lifetime().make_state<State>();
		state->quote = AddQuoteTracker(box, show, item, reply.quote);

		box->setTitle(reply.quote.empty()
			? tr::lng_reply_options_header()
			: tr::lng_reply_options_quote());

		auto save = state->quote.value(
		) | rpl::map([=](const TextWithEntities &quote) {
			return quote.empty()
				? tr::lng_settings_save()
				: tr::lng_reply_quote_selected();
		}) | rpl::flatten_latest();
		box->addButton(std::move(save), [=] {
			auto result = reply;
			result.quote = state->quote.current();
			finish(result);
		});

		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});

		session->data().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> removed) {
			return removed == item;
		}) | rpl::start_with_next([=] {
			finish({});
		}, box->lifetime());
	}));
}

} // namespace HistoryView::Controls

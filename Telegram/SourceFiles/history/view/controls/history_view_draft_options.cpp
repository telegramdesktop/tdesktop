/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_draft_options.h"

#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_drafts.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
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

enum class Section {
	Reply,
	Link,
};

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

[[nodiscard]] TextWithEntities HighlightParsedLinks(
		TextWithEntities text,
		const std::vector<MessageLinkRange> &links) {
	auto i = text.entities.begin();
	for (const auto &range : links) {
		if (range.custom.isEmpty()) {
			while (i != text.entities.end()) {
				if (i->offset() > range.start) {
					break;
				}
				++i;
			}
			i = text.entities.insert(
				i,
				EntityInText(EntityType::Url, range.start, range.length));
			++i;
		}
	}
	return text;
}

class PreviewWrap final : public Ui::RpWidget {
public:
	PreviewWrap(
		not_null<Ui::GenericBox*> box,
		not_null<History*> history);
	~PreviewWrap();

	[[nodiscard]] rpl::producer<SelectedQuote> showQuoteSelector(
		const SelectedQuote &quote);
	[[nodiscard]] rpl::producer<QString> showLinkSelector(
		const TextWithTags &message,
		Data::WebPageDraft webpage,
		const std::vector<MessageLinkRange> &links,
		const QString &usedLink);

private:
	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

	void initElement();
	void highlightUsedLink(
		const TextWithTags &message,
		const QString &usedLink,
		const std::vector<MessageLinkRange> &links);
	void startSelection(TextSelectType type);
	[[nodiscard]] TextSelection resolveNewSelection() const;

	const not_null<Ui::GenericBox*> _box;
	const not_null<History*> _history;
	const std::unique_ptr<Ui::ChatTheme> _theme;
	const std::unique_ptr<Ui::ChatStyle> _style;
	const std::unique_ptr<PreviewDelegate> _delegate;

	Section _section = Section::Reply;
	HistoryItem *_draftItem = nullptr;
	std::unique_ptr<Element> _element;
	rpl::variable<TextSelection> _selection;
	rpl::event_stream<QString> _chosenUrl;
	Ui::PeerUserpicView _userpic;
	rpl::lifetime _elementLifetime;

	QPoint _position;

	base::Timer _trippleClickTimer;
	ClickHandlerPtr _link;
	ClickHandlerPtr _pressedLink;
	TextSelectType _selectType = TextSelectType::Letters;
	uint16 _symbol = 0;
	uint16 _selectionStartSymbol = 0;
	bool _onlyMessageText = false;
	bool _afterSymbol = false;
	bool _selectionStartAfterSymbol = false;
	bool _over = false;
	bool _textCursor = false;
	bool _linkCursor = false;
	bool _selecting = false;

};

PreviewWrap::PreviewWrap(
	not_null<Ui::GenericBox*> box,
	not_null<History*> history)
: RpWidget(box)
, _box(box)
, _history(history)
, _theme(Window::Theme::DefaultChatThemeOn(lifetime()))
, _style(std::make_unique<Ui::ChatStyle>(
	history->session().colorIndicesValue()))
, _delegate(std::make_unique<PreviewDelegate>(
	box,
	_style.get(),
	[=] { update(); }))
, _position(0, st::msgMargin.bottom()) {
	_style->apply(_theme.get());

	const auto session = &_history->session();
	session->data().viewRepaintRequest(
	) | rpl::start_with_next([=](not_null<const Element*> view) {
		if (view == _element.get()) {
			update();
		}
	}, lifetime());

	_selection.changes() | rpl::start_with_next([=] {
		update();
	}, lifetime());

	_box->setAttribute(Qt::WA_OpaquePaintEvent, false);

	_box->paintRequest() | rpl::start_with_next([=](QRect clip) {
		const auto geometry = Ui::MapFrom(_box, this, rect());
		const auto fill = geometry.intersected(clip);
		if (!fill.isEmpty()) {
			auto p = QPainter(_box);
			p.setClipRect(fill);
			Window::SectionWidget::PaintBackground(
				p,
				_theme.get(),
				QSize(_box->width(), _box->window()->height()),
				fill);
		}
	}, lifetime());

	setMouseTracking(true);
}

PreviewWrap::~PreviewWrap() {
	_selection.reset(TextSelection());
	_elementLifetime.destroy();
	_element = nullptr;
	if (_draftItem) {
		_draftItem->destroy();
	}
}

rpl::producer<SelectedQuote> PreviewWrap::showQuoteSelector(
		const SelectedQuote &quote) {
	_selection.reset(TextSelection());

	const auto item = quote.item;
	const auto group = item->history()->owner().groups().find(item);
	const auto leader = group ? group->items.front().get() : item;
	_element = leader->createView(_delegate.get());
	_link = _pressedLink = nullptr;

	if (const auto was = base::take(_draftItem)) {
		was->destroy();
	}

	const auto media = item->media();
	_onlyMessageText = media
		&& (media->webpage()
			|| media->game()
			|| (!media->photo() && !media->document()));
	_section = Section::Reply;

	initElement();

	_selection = _element->selectionFromQuote(item, quote.text);
	return _selection.value(
	) | rpl::map([=](TextSelection selection) {
		if (const auto result = _element->selectedQuote(selection)) {
			return result;
		}
		return SelectedQuote{ item };
	});
}

rpl::producer<QString> PreviewWrap::showLinkSelector(
		const TextWithTags &message,
		Data::WebPageDraft webpage,
		const std::vector<MessageLinkRange> &links,
		const QString &usedLink) {
	_selection.reset(TextSelection());

	_element = nullptr;
	if (const auto was = base::take(_draftItem)) {
		was->destroy();
	}
	using Flag = MTPDmessageMediaWebPage::Flag;
	_draftItem = _history->addNewLocalMessage(
		_history->nextNonHistoryEntryId(),
		(MessageFlag::FakeHistoryItem
			| MessageFlag::Outgoing
			| MessageFlag::HasFromId
			| (webpage.invert ? MessageFlag::InvertMedia : MessageFlag())),
		UserId(), // via
		FullReplyTo(),
		base::unixtime::now(), // date
		_history->session().userPeerId(),
		QString(), // postAuthor
		HighlightParsedLinks({
			message.text,
			TextUtilities::ConvertTextTagsToEntities(message.tags),
		}, links),
		MTP_messageMediaWebPage(
			MTP_flags(Flag()
				| (webpage.forceLargeMedia
					? Flag::f_force_large_media
					: Flag())
				| (webpage.forceSmallMedia
					? Flag::f_force_small_media
					: Flag())),
			MTP_webPagePending(
				MTP_flags(webpage.url.isEmpty()
					? MTPDwebPagePending::Flag()
					: MTPDwebPagePending::Flag::f_url),
				MTP_long(webpage.id),
				MTP_string(webpage.url),
				MTP_int(0))),
		HistoryMessageMarkupData(),
		uint64(0)); // groupedId
	_element = _draftItem->createView(_delegate.get());
	_selectType = TextSelectType::Letters;
	_symbol = _selectionStartSymbol = 0;
	_afterSymbol = _selectionStartAfterSymbol = false;
	_section = Section::Link;

	initElement();
	highlightUsedLink(message, usedLink, links);

	return _chosenUrl.events();
}

void PreviewWrap::highlightUsedLink(
		const TextWithTags &message,
		const QString &usedLink,
		const std::vector<MessageLinkRange> &links) {
	auto selection = TextSelection();
	const auto view = QStringView(message.text);
	for (const auto &range : links) {
		auto text = view.mid(range.start, range.length);
		if (range.custom == usedLink
			|| (range.custom.isEmpty()
				&& range.length == usedLink.size()
				&& text == usedLink)) {
			selection = {
				uint16(range.start),
				uint16(range.start + range.length),
			};
			const auto skip = [](QChar ch) {
				return ch.isSpace() || Ui::Text::IsNewline(ch);
			};
			while (!text.isEmpty() && skip(text.front())) {
				text = text.mid(1);
				++selection.from;
			}
			while (!text.isEmpty() && skip(text.back())) {
				text = text.mid(0, text.size() - 1);
				--selection.to;
			}
			const auto basic = _element->textState(QPoint(0, 0), {
				.flags = Ui::Text::StateRequest::Flag::LookupSymbol,
				.onlyMessageText = true,
			});
			if (basic.symbol > 0) {
				selection.from += basic.symbol;
				selection.to += basic.symbol;
			}
			break;
		}
	}
	_selection = selection;
}

void PreviewWrap::paintEvent(QPaintEvent *e) {
	if (!_element) {
		return;
	}

	auto p = Painter(this);

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		e->rect(),
		!window()->isActiveWindow());
	context.outbg = _element->hasOutLayout();
	context.selection = _selecting
		? resolveNewSelection()
		: _selection.current();

	p.translate(_position);
	_element->draw(p, context);

	if (_element->displayFromPhoto()) {
		auto userpicBottom = height()
			- _element->marginBottom()
			- _element->marginTop();
		const auto item = _element->data();
		const auto userpicTop = userpicBottom - st::msgPhotoSize;
		if (const auto from = item->displayFrom()) {
			from->paintUserpicLeft(
				p,
				_userpic,
				st::historyPhotoLeft,
				userpicTop,
				width(),
				st::msgPhotoSize);
		} else if (const auto info = item->hiddenSenderInfo()) {
			if (info->customUserpic.empty()) {
				info->emptyUserpic.paintCircle(
					p,
					st::historyPhotoLeft,
					userpicTop,
					width(),
					st::msgPhotoSize);
			} else {
				const auto valid = info->paintCustomUserpic(
					p,
					_userpic,
					st::historyPhotoLeft,
					userpicTop,
					width(),
					st::msgPhotoSize);
				if (!valid) {
					info->customUserpic.load(
						&item->history()->session(),
						item->fullId());
				}
			}
		} else {
			Unexpected("Corrupt forwarded information in message.");
		}
	}
}

void PreviewWrap::leaveEventHook(QEvent *e) {
	if (!_element || !_over) {
		return;
	}
	_over = false;
	_textCursor = false;
	_linkCursor = false;
	if (!_selecting) {
		setCursor(style::cur_default);
	}
}

void PreviewWrap::mouseMoveEvent(QMouseEvent *e) {
	if (!_element) {
		return;
	}
	using Flag = Ui::Text::StateRequest::Flag;
	auto request = StateRequest{
		.flags = (_section == Section::Reply
			? Flag::LookupSymbol
			: Flag::LookupLink),
		.onlyMessageText = (_section == Section::Link || _onlyMessageText),
	};
	auto resolved = _element->textState(
		e->pos() - _position,
		request);
	_over = true;
	const auto text = (_section == Section::Reply)
		&& (resolved.cursor == CursorState::Text);
	_link = (_section == Section::Link && resolved.overMessageText)
		? resolved.link
		: nullptr;
	const auto link = (_link != nullptr) || (_pressedLink != nullptr);
	if (_textCursor != text || _linkCursor != link) {
		_textCursor = text;
		_linkCursor = link;
		setCursor((text || _selecting)
			? style::cur_text
			: link
			? style::cur_pointer
			: style::cur_default);
	}
	if (_symbol != resolved.symbol
		|| _afterSymbol != resolved.afterSymbol) {
		_symbol = resolved.symbol;
		_afterSymbol = resolved.afterSymbol;
		if (_selecting) {
			update();
		}
	}
}

void PreviewWrap::mousePressEvent(QMouseEvent *e) {
	if (!_over) {
		return;
	} else if (_section == Section::Reply) {
		startSelection(_trippleClickTimer.isActive()
			? TextSelectType::Paragraphs
			: TextSelectType::Letters);
	} else {
		_pressedLink = _link;
	}
}

void PreviewWrap::mouseReleaseEvent(QMouseEvent *e) {
	if (_section == Section::Reply) {
		if (!_selecting) {
			return;
		}
		const auto result = resolveNewSelection();
		_selecting = false;
		_selectType = TextSelectType::Letters;
		if (!_textCursor) {
			setCursor(style::cur_default);
		}
		_selection = result;
	} else if (base::take(_pressedLink) == _link && _link) {
		if (const auto url = _link->url(); !url.isEmpty()) {
			_chosenUrl.fire_copy(url);
		}
	} else if (!_link) {
		setCursor(style::cur_default);
	}
}

void PreviewWrap::mouseDoubleClickEvent(QMouseEvent *e) {
	if (!_over) {
		return;
	} else if (_section == Section::Reply) {
		startSelection(TextSelectType::Words);
		_trippleClickTimer.callOnce(QApplication::doubleClickInterval());
	}
}

void PreviewWrap::initElement() {
	_elementLifetime.destroy();

	if (!_element) {
		return;
	}
	_element->initDimensions();

	widthValue(
	) | rpl::filter([=](int width) {
		return width > st::msgMinWidth;
	}) | rpl::start_with_next([=](int width) {
		const auto height = _position.y()
			+ _element->resizeGetHeight(width)
			+ st::msgMargin.top();
		resize(width, height);
	}, _elementLifetime);
}

TextSelection PreviewWrap::resolveNewSelection() const {
	if (_section != Section::Reply) {
		return TextSelection();
	}
	const auto make = [](uint16 symbol, bool afterSymbol) {
		return uint16(symbol + (afterSymbol ? 1 : 0));
	};
	const auto first = make(_symbol, _afterSymbol);
	const auto second = make(
		_selectionStartSymbol,
		_selectionStartAfterSymbol);
	const auto result = (first <= second)
		? TextSelection{ first, second }
		: TextSelection{ second, first };
	return _element->adjustSelection(result, _selectType);
}

void PreviewWrap::startSelection(TextSelectType type) {
	if (_selecting && _selectType >= type) {
		return;
	}
	_selecting = true;
	_selectType = type;
	_selectionStartSymbol = _symbol;
	_selectionStartAfterSymbol = _afterSymbol;
	if (!_textCursor) {
		setCursor(style::cur_text);
	}
	update();
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
	return Context::Replies;
}

void AddFilledSkip(not_null<Ui::VerticalLayout*> container) {
	const auto skip = container->add(object_ptr<Ui::FixedHeightWidget>(
		container,
		st::settingsPrivacySkipTop));
	skip->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(skip).fillRect(clip, st::boxBg);
	}, skip->lifetime());
};

void DraftOptionsBox(
		not_null<Ui::GenericBox*> box,
		EditDraftOptionsArgs &&args,
		HistoryItem *replyItem,
		WebPageData *previewData) {
	box->setWidth(st::boxWideWidth);

	const auto &draft = args.draft;
	struct State {
		rpl::variable<Section> shown;
		rpl::lifetime shownLifetime;
		rpl::variable<SelectedQuote> quote;
		Data::WebPageDraft webpage;
		WebPageData *preview = nullptr;
		QString link;
		Ui::SettingsSlider *tabs = nullptr;
		PreviewWrap *wrap = nullptr;

		Fn<void(const QString &link, WebPageData *page)> performSwitch;
		Fn<void(const QString &link, bool force)> requestAndSwitch;
		rpl::lifetime resolveLifetime;
	};
	const auto state = box->lifetime().make_state<State>();
	state->quote = SelectedQuote{ replyItem, draft.reply.quote };
	state->webpage = draft.webpage;
	state->preview = previewData;
	state->shown = previewData ? Section::Link : Section::Reply;
	if (replyItem && previewData) {
		box->setNoContentMargin(true);
		state->tabs = box->setPinnedToTopContent(
			object_ptr<Ui::SettingsSlider>(
				box.get(),
				st::defaultTabsSlider));
		state->tabs->resizeToWidth(st::boxWideWidth);
		state->tabs->move(0, 0);
		state->tabs->setRippleTopRoundRadius(st::boxRadius);
		state->tabs->setSections({
			tr::lng_reply_header_short(tr::now),
			tr::lng_link_header_short(tr::now),
		});
		state->tabs->setActiveSectionFast(1);
		state->tabs->sectionActivated(
		) | rpl::start_with_next([=](int section) {
			state->shown = section ? Section::Link : Section::Reply;
		}, box->lifetime());
	} else {
		box->setTitle(previewData
			? tr::lng_link_options_header()
			: draft.reply.quote.empty()
			? tr::lng_reply_options_header()
			: tr::lng_reply_options_quote());
	}

	const auto bottom = box->setPinnedToBottomContent(
		object_ptr<Ui::VerticalLayout>(box));

	const auto &done = args.done;
	const auto &show = args.show;
	const auto &highlight = args.highlight;
	const auto &clearOldDraft = args.clearOldDraft;
	const auto resolveReply = [=] {
		auto result = draft.reply;
		if (const auto current = state->quote.current()) {
			result.messageId = current.item->fullId();
			result.quote = current.text;
		}
		return result;
	};
	const auto finish = [=](
			FullReplyTo result,
			Data::WebPageDraft webpage) {
		const auto weak = Ui::MakeWeak(box);
		done(std::move(result), std::move(webpage));
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	};
	const auto setupReplyActions = [=] {
		AddFilledSkip(bottom);

		const auto item = state->quote.current().item;
		if (item->allowsForward()) {
			Settings::AddButton(
				bottom,
				tr::lng_reply_in_another_chat(),
				st::settingsButton,
				{ &st::menuIconReplace }
			)->setClickedCallback([=] {
				ShowReplyToChatBox(show, resolveReply(), clearOldDraft);
			});
		}

		const auto weak = Ui::MakeWeak(box);
		Settings::AddButton(
			bottom,
			tr::lng_reply_show_in_chat(),
			st::settingsButton,
			{ &st::menuIconShowInChat }
		)->setClickedCallback([=] {
			highlight(resolveReply());
			if (const auto strong = weak.data()) {
				strong->closeBox();
			}
		});

		Settings::AddButton(
			bottom,
			tr::lng_reply_remove(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconDeleteAttention }
		)->setClickedCallback([=] {
			finish({}, state->webpage);
		});

		if (!item->originalText().empty()) {
			AddFilledSkip(bottom);
			Settings::AddDividerText(
				bottom,
				tr::lng_reply_about_quote());
		}
	};
	const auto setupLinkActions = [=] {
		AddFilledSkip(bottom);

		if (!draft.textWithTags.empty()) {
			Settings::AddButton(
				bottom,
				(state->webpage.invert
					? tr::lng_link_move_down()
					: tr::lng_link_move_up()),
				st::settingsButton,
				{ state->webpage.invert
					? &st::menuIconBelow
					: &st::menuIconAbove }
			)->setClickedCallback([=] {
				state->webpage.invert = !state->webpage.invert;
				state->webpage.manual = true;
				state->shown.force_assign(Section::Link);
			});
		}

		if (state->preview->hasLargeMedia) {
			const auto small = state->webpage.forceSmallMedia
				|| (!state->webpage.forceLargeMedia
					&& state->preview->computeDefaultSmallMedia());
			Settings::AddButton(
				bottom,
				(small
					? tr::lng_link_enlarge_photo()
					: tr::lng_link_shrink_photo()),
				st::settingsButton,
				{ small ? &st::menuIconEnlarge : &st::menuIconShrink }
			)->setClickedCallback([=] {
				if (small) {
					state->webpage.forceSmallMedia = false;
					state->webpage.forceLargeMedia = true;
				} else {
					state->webpage.forceLargeMedia = false;
					state->webpage.forceSmallMedia = true;
				}
				state->webpage.manual = true;
				state->shown.force_assign(Section::Link);
			});
		}

		Settings::AddButton(
			bottom,
			tr::lng_link_remove(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconDeleteAttention }
		)->setClickedCallback([=] {
			finish(resolveReply(), { .removed = true });
		});

		if (args.links.size() > 1) {
			AddFilledSkip(bottom);
			Settings::AddDividerText(
				bottom,
				tr::lng_link_about_choose());
		}
	};

	const auto &resolver = args.resolver;
	state->performSwitch = [=](const QString &link, WebPageData *page) {
		const auto now = base::unixtime::now();
		if (!page || (page->pendingTill > 0 && page->pendingTill < now)) {
			show->showToast(tr::lng_preview_cant(tr::now));
		} else if (page->pendingTill > 0) {
			const auto delay = std::max(page->pendingTill - now, TimeId());
			base::timer_once(
				(delay + 1) * crl::time(1000)
			) | rpl::start_with_next([=] {
				state->requestAndSwitch(link, true);
			}, state->resolveLifetime);

			page->owner().webPageUpdates(
			) | rpl::start_with_next([=](not_null<WebPageData*> updated) {
				if (updated == page && !updated->pendingTill) {
					state->resolveLifetime.destroy();
					state->performSwitch(link, page);
				}
			}, state->resolveLifetime);
		} else {
			state->preview = page;
			state->webpage.id = page->id;
			state->webpage.url = page->url;
			state->webpage.manual = true;
			state->link = link;
			state->shown.force_assign(Section::Link);
		}
	};
	state->requestAndSwitch = [=](const QString &link, bool force) {
		resolver->request(link, force);

		state->resolveLifetime = resolver->resolved(
		) | rpl::start_with_next([=](const QString &resolved) {
			if (resolved == link) {
				state->resolveLifetime.destroy();
				state->performSwitch(
					link,
					resolver->lookup(link).value_or(nullptr));
			}
		});
	};
	const auto switchTo = [=](const QString &link) {
		if (link == state->link) {
			return;
		} else if (const auto value = resolver->lookup(link)) {
			state->performSwitch(link, *value);
		} else {
			state->requestAndSwitch(link, false);
		}
	};

	state->wrap = box->addRow(
		object_ptr<PreviewWrap>(box, args.history),
		{});
	const auto &linkRanges = args.links;
	state->shown.value() | rpl::start_with_next([=](Section shown) {
		bottom->clear();
		state->shownLifetime.destroy();
		if (shown == Section::Reply) {
			state->quote = state->wrap->showQuoteSelector(
				state->quote.current());
			setupReplyActions();
		} else {
			state->wrap->showLinkSelector(
				draft.textWithTags,
				state->webpage,
				linkRanges,
				state->link
			) | rpl::start_with_next([=](QString link) {
				switchTo(link);
			}, state->shownLifetime);
			setupLinkActions();
		}
	}, box->lifetime());

	auto save = rpl::combine(
		state->quote.value(),
		state->shown.value()
	) | rpl::map([=](const SelectedQuote &quote, Section shown) {
		return (quote.text.empty() || shown != Section::Reply)
			? tr::lng_settings_save()
			: tr::lng_reply_quote_selected();
	}) | rpl::flatten_latest();
	box->addButton(std::move(save), [=] {
		finish(resolveReply(), state->webpage);
	});

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});

	if (replyItem) {
		args.show->session().data().itemRemoved(
		) | rpl::filter([=](not_null<const HistoryItem*> removed) {
			const auto current = state->quote.current().item;
			if ((removed == replyItem) || (removed == current)) {
				return true;
			}
			const auto group = current->history()->owner().groups().find(
				current);
			return (group && ranges::contains(group->items, removed));
		}) | rpl::start_with_next([=] {
			if (previewData) {
				state->tabs = nullptr;
				box->setPinnedToTopContent(
					object_ptr<Ui::RpWidget>(nullptr));
				box->setNoContentMargin(false);
				box->setTitle(state->quote.current().text.empty()
					? tr::lng_reply_options_header()
					: tr::lng_reply_options_quote());
				state->shown = Section::Link;
			} else {
				box->closeBox();
			}
		}, box->lifetime());
	}
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

		[[nodiscard]] rpl::producer<Chosen> singleChosen() const {
			return _singleChosen.events();
		}

		QString savedMessagesChatStatus() const override {
			return tr::lng_saved_quote_here(tr::now);
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

void EditDraftOptions(EditDraftOptionsArgs &&args) {
	const auto &draft = args.draft;
	const auto session = &args.show->session();
	const auto replyItem = session->data().message(draft.reply.messageId);
	const auto previewDataRaw = draft.webpage.id
		? session->data().webpage(draft.webpage.id).get()
		: nullptr;
	const auto previewData = (previewDataRaw
		&& !previewDataRaw->pendingTill
		&& !previewDataRaw->failed)
		? previewDataRaw
		: nullptr;
	if (!replyItem && !previewData) {
		return;
	}
	args.show->show(
		Box(DraftOptionsBox, std::move(args), replyItem, previewData));
}

} // namespace HistoryView::Controls

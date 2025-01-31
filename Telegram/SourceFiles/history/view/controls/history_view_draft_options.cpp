/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_draft_options.h"

#include "base/random.h"
#include "base/timer_rpl.h"
#include "base/unixtime.h"
#include "boxes/filters/edit_filter_chats_list.h"
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
#include "history/view/controls/history_view_forward_panel.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
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
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "window/themes/window_theme.h"
#include "window/section_widget.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
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
	Forward,
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

	[[nodiscard]] bool hasViewForItem(
		not_null<const HistoryItem*> item) const;

	void showForwardSelector(Data::ResolvedForwardDraft draft);
	[[nodiscard]] rpl::producer<SelectedQuote> showQuoteSelector(
		const SelectedQuote &quote);
	[[nodiscard]] rpl::producer<QString> showLinkSelector(
		const TextWithTags &message,
		Data::WebPageDraft webpage,
		const std::vector<MessageLinkRange> &links,
		const QString &usedLink);

	[[nodiscard]] rpl::producer<int> draggingScrollDelta() const {
		return _draggingScrollDelta.events();
	}

private:
	struct Entry {
		HistoryItem *item = nullptr;
		std::unique_ptr<Element> view;
	};

	void paintEvent(QPaintEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;

	void visibleTopBottomUpdated(int top, int bottom) override {
		_visibleTop = top;
		_visibleBottom = bottom;
	}

	void clear(std::vector<Entry> entries);
	void initElements();
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
	std::vector<Entry> _entries;
	base::flat_set<not_null<const Element*>> _views;
	rpl::variable<TextSelection> _selection;
	rpl::event_stream<QString> _chosenUrl;
	Ui::PeerUserpicView _userpic;
	rpl::lifetime _elementLifetime;

	QPoint _position;
	rpl::event_stream<int> _draggingScrollDelta;
	int _visibleTop = 0;
	int _visibleBottom = 0;

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
		if (_views.contains(view)) {
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
	base::take(_views);
	clear(base::take(_entries));
}

void PreviewWrap::clear(std::vector<Entry> entries) {
	_elementLifetime.destroy();
	for (auto &entry : entries) {
		entry.view = nullptr;
		if (const auto item = entry.item) {
			item->destroy();
		}
	}
}

bool PreviewWrap::hasViewForItem(not_null<const HistoryItem*> item) const {
	return (item->history() == _history)
		&& ranges::contains(_views, item, &Element::data);
}

void PreviewWrap::showForwardSelector(Data::ResolvedForwardDraft draft) {
	Expects(!draft.items.empty());

	_selection.reset(TextSelection());

	auto was = base::take(_entries);
	auto groups = base::flat_map<MessageGroupId, uint64>();
	const auto groupByItem = [&](not_null<HistoryItem*> item) {
		const auto groupId = item->groupId();
		if (!groupId) {
			return uint64();
		}
		auto i = groups.find(groupId);
		if (i == end(groups)) {
			i = groups.emplace(groupId, base::RandomValue<uint64>()).first;
		}
		return i->second;
	};
	const auto wasViews = base::take(_views);
	using Options = Data::ForwardOptions;
	const auto dropNames = (draft.options != Options::PreserveInfo);
	const auto dropCaptions = (draft.options == Options::NoNamesAndCaptions);
	for (const auto &source : draft.items) {
		const auto groupedId = groupByItem(source);
		const auto item = _history->addNewLocalMessage({
			.id = _history->nextNonHistoryEntryId(),
			.flags = (MessageFlag::FakeHistoryItem
				| MessageFlag::Outgoing
				| MessageFlag::HasFromId
				| (source->invertMedia()
					? MessageFlag::InvertMedia
					: MessageFlag())),
			.from = _history->session().userPeerId(),
			.date = base::unixtime::now(),
			.groupedId = groupedId,
			.ignoreForwardFrom = dropNames,
			.ignoreForwardCaptions = dropCaptions,
		}, source);
		_entries.push_back({ item });
	}
	for (auto &entry : _entries) {
		entry.view = entry.item->createView(_delegate.get());
		_views.emplace(entry.view.get());
	}
	_link = _pressedLink = nullptr;
	clear(std::move(was));

	_section = Section::Forward;

	initElements();
}

rpl::producer<SelectedQuote> PreviewWrap::showQuoteSelector(
		const SelectedQuote &quote) {
	_selection.reset(TextSelection());

	auto was = base::take(_entries);
	const auto wasViews = base::take(_views);
	const auto item = quote.item;
	const auto group = item->history()->owner().groups().find(item);
	const auto leader = group ? group->items.front().get() : item;
	_entries.push_back({
		.view = leader->createView(_delegate.get()),
	});
	_views.emplace(_entries.back().view.get());
	_link = _pressedLink = nullptr;
	clear(std::move(was));

	const auto media = item->media();
	_onlyMessageText = media
		&& (media->webpage()
			|| media->game()
			|| (!media->photo() && !media->document()));
	_section = Section::Reply;

	initElements();

	const auto view = _entries.back().view.get();
	_selection = view->selectionFromQuote(quote);
	return _selection.value(
	) | rpl::map([=](TextSelection selection) {
		if (const auto result = view->selectedQuote(selection)) {
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
	base::take(_views);
	clear(base::take(_entries));

	using Flag = MTPDmessageMediaWebPage::Flag;
	const auto item = _history->addNewLocalMessage({
		.id = _history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::Outgoing
			| MessageFlag::HasFromId
			| (webpage.invert ? MessageFlag::InvertMedia : MessageFlag())),
		.from = _history->session().userPeerId(),
		.date = base::unixtime::now(),
	}, HighlightParsedLinks({
		message.text,
		TextUtilities::ConvertTextTagsToEntities(message.tags),
	}, links), MTP_messageMediaWebPage(
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
			MTP_int(0))));
	_entries.push_back({ item, item->createView(_delegate.get()) });
	_views.emplace(_entries.back().view.get());

	_selectType = TextSelectType::Letters;
	_symbol = _selectionStartSymbol = 0;
	_afterSymbol = _selectionStartAfterSymbol = false;
	_section = Section::Link;

	initElements();
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
			const auto view = _entries.back().view.get();
			const auto basic = view->textState(QPoint(0, 0), {
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
	auto p = Painter(this);

	p.translate(_position);

	auto context = _theme->preparePaintContext(
		_style.get(),
		rect(),
		e->rect(),
		!window()->isActiveWindow());
	for (const auto &entry : _entries) {
		context.outbg = entry.view->hasOutLayout();
		context.selection = _selecting
			? resolveNewSelection()
			: _selection.current();

		entry.view->draw(p, context);

		p.translate(0, entry.view->height());
	}
	const auto top = _entries.empty() ? nullptr : _entries.back().view.get();
	if (top && top->displayFromPhoto()) {
		auto userpicBottom = height()
			- top->marginBottom()
			- top->marginTop();
		const auto item = top->data();
		const auto userpicTop = userpicBottom - st::msgPhotoSize;
		if (const auto from = item->displayFrom()) {
			from->paintUserpicLeft(
				p,
				_userpic,
				st::historyPhotoLeft,
				userpicTop,
				width(),
				st::msgPhotoSize);
		} else if (const auto info = item->displayHiddenSenderInfo()) {
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
	if (!_over) {
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
	if (_entries.empty()) {
		return;
	}
	using Flag = Ui::Text::StateRequest::Flag;
	auto request = StateRequest{
		.flags = (_section == Section::Reply
			? Flag::LookupSymbol
			: Flag::LookupLink),
		.onlyMessageText = (_section == Section::Link || _onlyMessageText),
	};
	const auto position = e->pos();
	auto local = position - _position;
	auto resolved = TextState();
	for (auto &entry : _entries) {
		const auto height = entry.view->height();
		if (local.y() < height) {
			resolved = entry.view->textState(local, request);
			break;
		}
		local.setY(local.y() - height);
	}
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

	_draggingScrollDelta.fire([&] {
		if (!_selecting || _visibleTop >= _visibleBottom) {
			return 0;
		} else if (position.y() < _visibleTop) {
			return position.y() - _visibleTop;
		} else if (position.y() >= _visibleBottom) {
			return position.y() + 1 - _visibleBottom;
		}
		return 0;
	}());
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

void PreviewWrap::initElements() {
	for (auto &entry : _entries) {
		entry.view->initDimensions();
	}
	widthValue(
	) | rpl::filter([=](int width) {
		return width > st::msgMinWidth;
	}) | rpl::start_with_next([=](int width) {
		auto height = _position.y();
		for (const auto &entry : _entries) {
			height += entry.view->resizeGetHeight(width);
		}
		height += st::msgMargin.top();
		resize(width, height);
	}, _elementLifetime);
}

TextSelection PreviewWrap::resolveNewSelection() const {
	if (_section != Section::Reply || _entries.empty()) {
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
	return _entries.back().view->adjustSelection(result, _selectType);
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
		rpl::variable<Section> shown = Section::Link;
		rpl::lifetime shownLifetime;
		rpl::variable<SelectedQuote> quote;
		Data::ResolvedForwardDraft forward;
		Data::WebPageDraft webpage;
		WebPageData *preview = nullptr;
		QString link;
		Ui::SettingsSlider *tabs = nullptr;
		PreviewWrap *wrap = nullptr;

		Fn<void(const QString &link, WebPageData *page)> performSwitch;
		Fn<void(const QString &link, bool force)> requestAndSwitch;
		rpl::lifetime resolveLifetime;

		Fn<void()> rebuild;
	};
	const auto state = box->lifetime().make_state<State>();
	state->link = args.usedLink;
	state->quote = SelectedQuote{
		replyItem,
		draft.reply.quote,
		draft.reply.quoteOffset,
	};
	state->forward = std::move(args.forward);
	state->webpage = draft.webpage;
	state->preview = previewData;

	state->rebuild = [=] {
		const auto hasLink = (state->preview != nullptr);
		const auto hasReply = (state->quote.current().item != nullptr);
		const auto hasForward = !state->forward.items.empty();
		if (!hasLink && !hasReply && !hasForward) {
			box->closeBox();
			return;
		}
		const auto section = state->shown.current();
		const auto changeSection = (section == Section::Link)
			? !hasLink
			: (section == Section::Reply)
			? !hasReply
			: !hasForward;
		const auto now = !changeSection
			? section
			: hasLink
			? Section::Link
			: hasReply
			? Section::Reply
			: Section::Forward;
		auto labels = std::vector<QString>();
		auto indices = base::flat_map<Section, int>();
		auto sections = std::vector<Section>();
		const auto push = [&](Section section, tr::phrase<> phrase) {
			indices[section] = labels.size();
			labels.push_back(phrase(tr::now));
			sections.push_back(section);
		};
		if (hasLink) {
			push(Section::Link, tr::lng_link_header_short);
		}
		if (hasReply) {
			push(Section::Reply, tr::lng_reply_header_short);
		}
		if (hasForward) {
			push(Section::Forward, tr::lng_forward_header_short);
		}
		if (labels.size() > 1) {
			box->setNoContentMargin(true);
			state->tabs = box->setPinnedToTopContent(
				object_ptr<Ui::SettingsSlider>(
					box.get(),
					st::defaultTabsSlider));
			state->tabs->resizeToWidth(st::boxWideWidth);
			state->tabs->move(0, 0);
			state->tabs->setRippleTopRoundRadius(st::boxRadius);
			state->tabs->setSections(labels);
			state->tabs->setActiveSectionFast(indices[now]);
			state->tabs->sectionActivated(
			) | rpl::start_with_next([=](int index) {
				state->shown = sections[index];
			}, box->lifetime());
		} else {
			const auto forwardCount = state->forward.items.size();
			box->setTitle(hasLink
				? tr::lng_link_options_header()
				: hasReply
				? (state->quote.current().text.empty()
					? tr::lng_reply_options_header()
					: tr::lng_reply_options_quote())
				: (forwardCount == 1)
				? tr::lng_forward_title()
				: tr::lng_forward_many_title(
					lt_count,
					rpl::single(forwardCount * 1.0)));
		}
		state->shown.force_assign(now);
	};
	state->rebuild();

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
			result.quoteOffset = current.offset;
		} else {
			result.quote = {};
		}
		return result;
	};
	const auto finish = [=](
			FullReplyTo result,
			Data::WebPageDraft webpage,
			std::optional<Data::ForwardOptions> options) {
		const auto weak = Ui::MakeWeak(box);
		auto forward = Data::ForwardDraft();
		if (options) {
			forward.options = *options;
			for (const auto &item : state->forward.items) {
				forward.ids.push_back(item->fullId());
			}
		}
		done(std::move(result), std::move(webpage), std::move(forward));
		if (const auto strong = weak.data()) {
			strong->closeBox();
		}
	};
	const auto setupReplyActions = [=] {
		AddFilledSkip(bottom);

		const auto item = state->quote.current().item;
		if (item->allowsForward()) {
			Settings::AddButtonWithIcon(
				bottom,
				tr::lng_reply_in_another_chat(),
				st::settingsButton,
				{ &st::menuIconReplace }
			)->setClickedCallback([=] {
				ShowReplyToChatBox(show, resolveReply(), clearOldDraft);
			});
		}

		const auto weak = Ui::MakeWeak(box);
		Settings::AddButtonWithIcon(
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

		Settings::AddButtonWithIcon(
			bottom,
			tr::lng_reply_remove(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconDeleteAttention }
		)->setClickedCallback([=] {
			finish({}, state->webpage, state->forward.options);
		});

		if (!item->originalText().empty()) {
			AddFilledSkip(bottom);
			Ui::AddDividerText(bottom, tr::lng_reply_about_quote());
		}
	};
	const auto setupLinkActions = [=] {
		AddFilledSkip(bottom);

		if (!draft.textWithTags.empty()) {
			Settings::AddButtonWithIcon(
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
			Settings::AddButtonWithIcon(
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

		Settings::AddButtonWithIcon(
			bottom,
			tr::lng_link_remove(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconDeleteAttention }
		)->setClickedCallback([=] {
			const auto options = state->forward.options;
			finish(resolveReply(), { .removed = true }, options);
		});

		if (args.links.size() > 1) {
			AddFilledSkip(bottom);
			Ui::AddDividerText(bottom, tr::lng_link_about_choose());
		}
	};

	const auto setupForwardActions = [=] {
		using Options = Data::ForwardOptions;
		const auto now = state->forward.options;
		const auto &items = state->forward.items;
		const auto count = items.size();
		const auto dropNames = (now != Options::PreserveInfo);
		const auto sendersCount = ItemsForwardSendersCount(items);
		const auto captionsCount = ItemsForwardCaptionsCount(items);
		const auto hasOnlyForcedForwardedInfo = !captionsCount
			&& HasOnlyForcedForwardedInfo(items);
		const auto dropCaptions = (now == Options::NoNamesAndCaptions);

		AddFilledSkip(bottom);

		if (!hasOnlyForcedForwardedInfo
			&& !HasOnlyDroppedForwardedInfo(items)) {
			Settings::AddButtonWithIcon(
				bottom,
				(dropNames
					? (sendersCount == 1
						? tr::lng_forward_action_show_sender
						: tr::lng_forward_action_show_senders)
					: (sendersCount == 1
						? tr::lng_forward_action_hide_sender
						: tr::lng_forward_action_hide_senders))(),
				st::settingsButton,
				{ dropNames
					? &st::menuIconUserShow
					: &st::menuIconUserHide }
			)->setClickedCallback([=] {
				state->forward.options = dropNames
					? Options::PreserveInfo
					: Options::NoSenderNames;
				state->shown.force_assign(Section::Forward);
			});
		}
		if (captionsCount) {
			Settings::AddButtonWithIcon(
				bottom,
				(dropCaptions
					? (captionsCount == 1
						? tr::lng_forward_action_show_caption
						: tr::lng_forward_action_show_captions)
					: (captionsCount == 1
						? tr::lng_forward_action_hide_caption
						: tr::lng_forward_action_hide_captions))(),
				st::settingsButton,
				{ dropCaptions
					? &st::menuIconCaptionShow
					: &st::menuIconCaptionHide }
			)->setClickedCallback([=] {
				state->forward.options = dropCaptions
					? Options::NoSenderNames
					: Options::NoNamesAndCaptions;
				state->shown.force_assign(Section::Forward);
			});
		}

		Settings::AddButtonWithIcon(
			bottom,
			tr::lng_forward_action_change_recipient(),
			st::settingsButton,
			{ &st::menuIconReplace }
		)->setClickedCallback([=] {
			auto draft = base::take(state->forward);
			finish(resolveReply(), state->webpage, std::nullopt);
			Window::ShowForwardMessagesBox(show, {
				.ids = show->session().data().itemsToIds(draft.items),
				.options = draft.options,
			});
		});

		Settings::AddButtonWithIcon(
			bottom,
			tr::lng_forward_action_remove(),
			st::settingsAttentionButtonWithIcon,
			{ &st::menuIconDeleteAttention }
		)->setClickedCallback([=] {
			finish(resolveReply(), state->webpage, std::nullopt);
		});

		AddFilledSkip(bottom);
		Ui::AddDividerText(bottom, (count == 1
			? tr::lng_forward_about()
			: tr::lng_forward_many_about()));
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
	state->wrap->draggingScrollDelta(
	) | rpl::start_with_next([=](int delta) {
		box->scrollByDraggingDelta(delta);
	}, state->wrap->lifetime());

	const auto &linkRanges = args.links;
	state->shown.value() | rpl::start_with_next([=](Section shown) {
		bottom->clear();
		state->shownLifetime.destroy();
		switch (shown) {
			case Section::Reply: {
				state->quote = state->wrap->showQuoteSelector(
					state->quote.current());
				setupReplyActions();
			} break;
			case Section::Link: {
				state->wrap->showLinkSelector(
					draft.textWithTags,
					state->webpage,
					linkRanges,
					state->link
				) | rpl::start_with_next([=](QString link) {
					switchTo(link);
				}, state->shownLifetime);
				setupLinkActions();
			} break;
			case Section::Forward: {
				state->wrap->showForwardSelector(state->forward);
				setupForwardActions();
			} break;
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
	const auto submit = [=] {
		if (state->quote.current().overflown) {
			show->showToast({
				.title = tr::lng_reply_quote_long_title(tr::now),
				.text = { tr::lng_reply_quote_long_text(tr::now) },
			});
		} else {
			const auto options = state->forward.options;
			finish(resolveReply(), state->webpage, options);
		}
	};
	box->addButton(std::move(save), submit);

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});

	box->events() | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get())->key();
			if (key == Qt::Key_Enter || key == Qt::Key_Return) {
				submit();
			}
		}
	}, box->lifetime());

	args.show->session().data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> removed) {
		const auto inReply = (state->quote.current().item == removed);
		if (inReply) {
			state->quote = SelectedQuote();
		}
		const auto i = ranges::find(state->forward.items, removed);
		const auto inForward = (i != end(state->forward.items));
		if (inForward) {
			state->forward.items.erase(i);
		}
		if (inReply || inForward) {
			state->rebuild();
		}
	}, box->lifetime());

	args.show->session().data().itemViewRefreshRequest(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		if (state->wrap->hasViewForItem(item)) {
			state->rebuild();
		}
	}, box->lifetime());

}

struct AuthorSelector {
	object_ptr<Ui::RpWidget> content = { nullptr };
	Fn<bool(int, int, int)> overrideKey;
	Fn<void()> activate;
};
[[nodiscard]] AuthorSelector AuthorRowSelector(
		not_null<Main::Session*> session,
		FullReplyTo reply,
		Fn<void(not_null<Data::Thread*>)> chosen) {
	const auto item = session->data().message(reply.messageId);
	if (!item) {
		return {};
	}
	const auto displayFrom = item->displayFrom();
	const auto from = displayFrom ? displayFrom : item->from().get();
	if (!from->isUser() || from == item->history()->peer || from->isSelf()) {
		return {};
	}

	class AuthorController final : public PeerListController {
	public:
		AuthorController(not_null<PeerData*> peer, Fn<void()> click)
		: _peer(peer)
		, _click(std::move(click)) {
		}

		void prepare() override {
			delegate()->peerListAppendRow(
				std::make_unique<ChatsListBoxController::Row>(
					_peer->owner().history(_peer),
					&computeListSt().item));
			delegate()->peerListRefreshRows();
			TrackPremiumRequiredChanges(this, _lifetime);
		}
		void loadMoreRows() override {
		}
		void rowClicked(not_null<PeerListRow*> row) override {
			if (RecipientRow::ShowLockedError(this, row, WritePremiumRequiredError)) {
				return;
			} else if (const auto onstack = _click) {
				onstack();
			}
		}
		Main::Session &session() const override {
			return _peer->session();
		}

	private:
		const not_null<PeerData*> _peer;
		Fn<void()> _click;
		rpl::lifetime _lifetime;

	};

	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_reply_in_author()));
	Ui::AddSkip(container);

	const auto activate = [=] {
		chosen(from->owner().history(from));
	};
	const auto delegate = container->lifetime().make_state<
		PeerListContentDelegateSimple
	>();
	const auto controller = container->lifetime().make_state<
		AuthorController
	>(from, activate);
	controller->setStyleOverrides(&st::peerListSingleRow);
	const auto content = container->add(object_ptr<PeerListContent>(
		container,
		controller));
	delegate->setContent(content);
	controller->setDelegate(delegate);

	Ui::AddSkip(container);
	container->add(CreatePeerListSectionSubtitle(
		container,
		tr::lng_reply_in_chats_list()));

	const auto overrideKey = [=](int direction, int from, int to) {
		if (!content->isVisible()) {
			return false;
		} else if (direction > 0 && from < 0 && to >= 0) {
			if (content->hasSelection()) {
				const auto was = content->selectedIndex();
				const auto now = content->selectSkip(1).reallyMovedTo;
				if (was != now) {
					return true;
				}
				content->clearSelection();
			} else {
				content->selectSkip(1);
				return true;
			}
		} else if (direction < 0 && to < 0) {
			if (!content->hasSelection()) {
				content->selectLast();
			} else if (from >= 0 || content->hasSelection()) {
				content->selectSkip(-1);
			}
		}
		return false;
	};

	return {
		.content = std::move(result),
		.overrideKey = overrideKey,
		.activate = activate,
	};
}

} // namespace

void ShowReplyToChatBox(
		std::shared_ptr<ChatHelpers::Show> show,
		FullReplyTo reply,
		Fn<void()> clearOldDraft) {
	class Controller final : public ChooseRecipientBoxController {
	public:
		using Chosen = not_null<Data::Thread*>;

		Controller(not_null<Main::Session*> session, FullReplyTo reply)
		: ChooseRecipientBoxController({
			.session = session,
			.callback = [=](Chosen thread) {
				_singleChosen.fire_copy(thread);
			},
			.premiumRequiredError = WritePremiumRequiredError,
		}) {
			_authorRow = AuthorRowSelector(
				session,
				reply,
				[=](Chosen thread) { _singleChosen.fire_copy(thread); });
			if (_authorRow.content) {
				setStyleOverrides(&st::peerListSmallSkips);
			}
		}

		void noSearchSubmit() {
			if (const auto onstack = _authorRow.activate) {
				onstack();
			}
		}

		[[nodiscard]] rpl::producer<Chosen> singleChosen() const {
			return _singleChosen.events();
		}

		QString savedMessagesChatStatus() const override {
			return tr::lng_saved_quote_here(tr::now);
		}

		bool overrideKeyboardNavigation(
				int direction,
				int fromIndex,
				int toIndex) override {
			return _authorRow.overrideKey
				&& _authorRow.overrideKey(direction, fromIndex, toIndex);
		}

	private:
		void prepareViewHook() override {
			if (_authorRow.content) {
				delegate()->peerListSetAboveWidget(
					std::move(_authorRow.content));
			}
			ChooseRecipientBoxController::prepareViewHook();
			delegate()->peerListSetTitle(tr::lng_reply_in_another_title());
		}

		rpl::event_stream<Chosen> _singleChosen;
		AuthorSelector _authorRow;

	};

	struct State {
		not_null<PeerListBox*> box;
		not_null<Controller*> controller;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto session = &show->session();
	const auto state = [&] {
		auto controller = std::make_unique<Controller>(session, reply);
		const auto controllerRaw = controller.get();
		auto box = Box<PeerListBox>(std::move(controller), [=](
				not_null<PeerListBox*> box) {
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });

			box->noSearchSubmits() | rpl::start_with_next([=] {
				controllerRaw->noSearchSubmit();
			}, box->lifetime());
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
	if (!replyItem && !previewData && args.forward.items.empty()) {
		return;
	}
	args.show->show(
		Box(DraftOptionsBox, std::move(args), replyItem, previewData));
}

} // namespace HistoryView::Controls

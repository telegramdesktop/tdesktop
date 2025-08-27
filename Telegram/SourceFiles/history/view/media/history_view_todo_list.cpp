/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_todo_list.h"

#include "base/unixtime.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h" // TextContext
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_text_helper.h"
#include "calls/calls_instance.h"
#include "ui/chat/message_bubble.h"
#include "ui/chat/chat_style.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/text/format_values.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/fireworks_animation.h"
#include "ui/toast/toast.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "data/data_media_types.h"
#include "data/data_poll.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "base/unixtime.h"
#include "base/timer.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_todo_lists.h"
#include "window/window_peer_menu.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace HistoryView {
namespace {

constexpr auto kShowRecentVotersCount = 3;
constexpr auto kRotateSegments = 8;
constexpr auto kRotateAmplitude = 3.;
constexpr auto kScaleSegments = 2;
constexpr auto kScaleAmplitude = 0.03;
constexpr auto kLargestRadialDuration = 30 * crl::time(1000);
constexpr auto kCriticalCloseDuration = 5 * crl::time(1000);

} // namespace

struct TodoList::Task {
	Task();

	void fillData(
		not_null<Element*> view,
		not_null<TodoListData*> todolist,
		const TodoListItem &original,
		Ui::Text::MarkedContext context);
	void setCompletedBy(PeerData *by);

	Ui::Text::String text;
	Ui::Text::String name;
	PeerData *completedBy = nullptr;
	mutable Ui::PeerUserpicView userpic;
	TimeId completionDate = 0;
	int id = 0;
	ClickHandlerPtr handler;
	Ui::Animations::Simple selectedAnimation;
	mutable std::unique_ptr<Ui::RippleAnimation> ripple;
};

TodoList::Task::Task()
: text(st::msgMinWidth / 2)
, name(st::msgMinWidth / 2) {
}

void TodoList::Task::fillData(
		not_null<Element*> view,
		not_null<TodoListData*> todolist,
		const TodoListItem &original,
		Ui::Text::MarkedContext context) {
	id = original.id;
	setCompletedBy(original.completedBy);
	completionDate = original.completionDate;
	if (!text.isEmpty() && text.toTextWithEntities() == original.text) {
		return;
	}
	text.setMarkedText(
		st::historyPollAnswerStyle,
		original.text,
		Ui::WebpageTextTitleOptions(),
		context);
	InitElementTextPart(view, text);
}

void TodoList::Task::setCompletedBy(PeerData *by) {
	if (!by || completedBy == by) {
		return;
	}
	completedBy = by;
	name.setText(st::historyPollAnswerStyle, completedBy->name());
}

TodoList::TodoList(
	not_null<Element*> parent,
	not_null<TodoListData*> todolist,
	Element *replacing)
: Media(parent)
, _todolist(todolist)
, _title(st::msgMinWidth / 2) {
	history()->owner().registerTodoListView(_todolist, _parent);
	if (const auto media = replacing ? replacing->media() : nullptr) {
		const auto info = media->takeTasksInfo();
		if (!info.empty()) {
			setupPreviousState(info);
		}
	}
}

void TodoList::setupPreviousState(const std::vector<TodoTaskInfo> &info) {
	// If we restore state from the view we're replacing we'll be able to
	// animate the changes properly.
	updateTasks(true);
	for (auto &task : _tasks) {
		const auto i = ranges::find(info, task.id, &TodoTaskInfo::id);
		if (i != end(info)) {
			task.setCompletedBy(i->completedBy);
			task.completionDate = i->completionDate;
		}
	}
}

QSize TodoList::countOptimalSize() {
 	updateTexts();

	const auto paddings = st::msgPadding.left() + st::msgPadding.right();

	auto maxWidth = st::msgFileMinWidth;
	accumulate_max(maxWidth, paddings + _title.maxWidth());
	for (const auto &task : _tasks) {
		accumulate_max(
			maxWidth,
			paddings
			+ st::historyChecklistTaskPadding.left()
			+ task.text.maxWidth()
			+ st::historyChecklistTaskPadding.right());
	}

	const auto tasksHeight = ranges::accumulate(ranges::views::all(
		_tasks
	) | ranges::views::transform([](const Task &task) {
		return st::historyChecklistTaskPadding.top()
			+ task.text.minHeight()
			+ st::historyChecklistTaskPadding.bottom();
	}), 0);

	const auto bottomButtonHeight = st::historyPollBottomButtonSkip;
	auto minHeight = st::historyPollQuestionTop
		+ _title.minHeight()
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip
		+ tasksHeight
		+ st::historyPollTotalVotesSkip
		+ bottomButtonHeight
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}
	return { maxWidth, minHeight };
}

bool TodoList::canComplete() const {
	return (_parent->data()->out()
		|| _parent->history()->peer->isSelf()
		|| _todolist->othersCanComplete())
		&& _parent->data()->isRegular()
		&& !_parent->data()->Has<HistoryMessageForwarded>();
}

int TodoList::countTaskTop(
		const Task &task,
		int innerWidth) const {
	auto tshift = st::historyPollQuestionTop;
	if (!isBubbleTop()) {
		tshift -= st::msgFileTopMinus;
	}
	tshift += _title.countHeight(innerWidth) + st::historyPollSubtitleSkip;
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;
	const auto i = ranges::find(
		_tasks,
		&task,
		[](const Task &task) { return &task; });
	const auto countHeight = [&](const Task &task) {
		return countTaskHeight(task, innerWidth);
	};
	tshift += ranges::accumulate(
		begin(_tasks),
		i,
		0,
		ranges::plus(),
		countHeight);
	return tshift;
}

int TodoList::countTaskHeight(
		const Task &task,
		int innerWidth) const {
	const auto answerWidth = innerWidth
		- st::historyChecklistTaskPadding.left()
		- st::historyChecklistTaskPadding.right();
	return st::historyChecklistTaskPadding.top()
		+ task.text.countHeight(answerWidth)
		+ st::historyChecklistTaskPadding.bottom();
}

QSize TodoList::countCurrentSize(int newWidth) {
	accumulate_min(newWidth, maxWidth());
	const auto innerWidth = newWidth
		- st::msgPadding.left()
		- st::msgPadding.right();

	const auto tasksHeight = ranges::accumulate(ranges::views::all(
		_tasks
	) | ranges::views::transform([&](const Task &task) {
		return countTaskHeight(task, innerWidth);
	}), 0);

	const auto bottomButtonHeight = st::historyPollBottomButtonSkip;
	auto newHeight = st::historyPollQuestionTop
		+ _title.countHeight(innerWidth)
		+ st::historyPollSubtitleSkip
		+ st::msgDateFont->height
		+ st::historyPollAnswersSkip
		+ tasksHeight
		+ st::historyPollTotalVotesSkip
		+ bottomButtonHeight
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
	if (!isBubbleTop()) {
		newHeight -= st::msgFileTopMinus;
	}
	return { newWidth, newHeight };
}

void TodoList::updateTexts() {
	if (_todoListVersion == _todolist->version) {
		return;
	}
	const auto skipAnimations = _tasks.empty();
	_todoListVersion = _todolist->version;

	if (_title.toTextWithEntities() != _todolist->title) {
		auto options = Ui::WebpageTextTitleOptions();
		options.maxw = options.maxh = 0;
		_title.setMarkedText(
			st::historyPollQuestionStyle,
			_todolist->title,
			options,
			Core::TextContext({
				.session = &_todolist->session(),
				.repaint = [=] { repaint(); },
				.customEmojiLoopLimit = 2,
			}));
		InitElementTextPart(_parent, _title);
	}
	if (_flags != _todolist->flags() || _subtitle.isEmpty()) {
		_flags = _todolist->flags();
		_subtitle.setText(
			st::msgDateTextStyle,
			(_todolist->othersCanComplete()
				? tr::lng_todo_title_group(tr::now)
				: tr::lng_todo_title(tr::now)));
	}
	updateTasks(skipAnimations);
}

void TodoList::updateTasks(bool skipAnimations) {
	const auto context = Core::TextContext({
		.session = &_todolist->session(),
		.repaint = [=] { repaint(); },
		.customEmojiLoopLimit = 2,
	});
	const auto changed = !ranges::equal(
		_tasks,
		_todolist->items,
		ranges::equal_to(),
		&Task::id,
		&TodoListItem::id);
	if (!changed) {
		auto animated = false;
		auto &&tasks = ranges::views::zip(_tasks, _todolist->items);
		for (auto &&[task, original] : tasks) {
			const auto wasDate = task.completionDate;
			task.fillData(_parent, _todolist, original, context);
			if (!skipAnimations && (!wasDate != !task.completionDate)) {
				startToggleAnimation(task);
				animated = true;
			}
		}
		updateCompletionStatus();
		if (animated) {
			maybeStartFireworks();
		}
		return;
	}
	_tasks = ranges::views::all(
		_todolist->items
	) | ranges::views::transform([&](const TodoListItem &item) {
		auto result = Task();
		result.id = item.id;
		result.fillData(_parent, _todolist, item, context);
		return result;
	}) | ranges::to_vector;

	for (auto &task : _tasks) {
		task.handler = createTaskClickHandler(task);
	}

	updateCompletionStatus();
}

ClickHandlerPtr TodoList::createTaskClickHandler(
		const Task &task) {
	const auto id = task.id;
	auto result = std::make_shared<LambdaClickHandler>(crl::guard(this, [=] {
		toggleCompletion(id);
	}));
	result->setProperty(kTodoListItemIdProperty, id);
	return result;
}

void TodoList::startToggleAnimation(Task &task) {
	const auto selected = (task.completionDate != 0);
	task.selectedAnimation.start(
		[=] { repaint(); },
		selected ? 0. : 1.,
		selected ? 1. : 0.,
		st::defaultCheck.duration);
}

void TodoList::toggleCompletion(int id) {
	if (_parent->data()->isBusinessShortcut()) {
		return;
	} else if (_parent->data()->Has<HistoryMessageForwarded>()) {
		_parent->delegate()->elementShowTooltip(
			tr::lng_todo_mark_forwarded(tr::now, Ui::Text::RichLangValue),
			[] {});
		return;
	} else if (!canComplete()) {
		_parent->delegate()->elementShowTooltip(
			tr::lng_todo_mark_restricted(
				tr::now,
				lt_user,
				Ui::Text::Bold(_parent->data()->from()->shortName()),
				Ui::Text::RichLangValue), [] {});
		return;
	} else if (!_parent->history()->session().premium()) {
		Window::PeerMenuTodoWantsPremium(Window::TodoWantsPremium::Mark);
		return;
	}
	const auto i = ranges::find(
		_tasks,
		id,
		&Task::id);
	if (i == end(_tasks)) {
		return;
	}

	const auto selected = (i->completionDate != 0);
	i->completionDate = selected ? TimeId() : base::unixtime::now();
	if (!selected) {
		i->setCompletedBy(_parent->history()->session().user());
	}

	const auto parentMedia = _parent->data()->media();
	const auto baseList = parentMedia ? parentMedia->todolist() : nullptr;
	if (baseList) {
		const auto j = ranges::find(baseList->items, id, &TodoListItem::id);
		if (j != end(baseList->items)) {
			j->completionDate = i->completionDate;
			j->completedBy = i->completedBy;
		}
		history()->owner().updateDependentMessages(_parent->data());
	}

	startToggleAnimation(*i);
	repaint();

	history()->session().api().todoLists().toggleCompletion(
		_parent->data()->fullId(),
		id,
		!selected);

	maybeStartFireworks();
}

void TodoList::maybeStartFireworks() {
	if (!ranges::contains(_tasks, TimeId(), &Task::completionDate)) {
		_fireworksAnimation = std::make_unique<Ui::FireworksAnimation>(
			[=] { repaint(); });
	}
}

void TodoList::updateCompletionStatus() {
	const auto incompleted = int(ranges::count(
		_todolist->items,
		nullptr,
		&TodoListItem::completedBy));
	const auto total = int(_todolist->items.size());
	if (_total == total
		&& _incompleted == incompleted
		&& !_completionStatusLabel.isEmpty()) {
		return;
	}
	_total = total;
	_incompleted = incompleted;
	const auto totalText = QString::number(total);
	const auto string = (incompleted == total)
		? tr::lng_todo_completed_none(tr::now, lt_total, totalText)
		: tr::lng_todo_completed(
			tr::now,
			lt_count,
			total - incompleted,
			lt_total,
			totalText);
	_completionStatusLabel.setText(st::msgDateTextStyle, string);
}

void TodoList::draw(Painter &p, const PaintContext &context) const {
	if (width() < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	auto paintw = width();

	const auto stm = context.messageStyle();
	const auto padding = st::msgPadding;
	auto tshift = st::historyPollQuestionTop;
	if (!isBubbleTop()) {
		tshift -= st::msgFileTopMinus;
	}
	paintw -= padding.left() + padding.right();

	p.setPen(stm->historyTextFg);
	_title.draw(p, {
		.position = { padding.left(), tshift },
		.availableWidth = paintw,
		.palette = &stm->textPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
		.selection = context.selection,
	});
	tshift += _title.countHeight(paintw) + st::historyPollSubtitleSkip;

	p.setPen(stm->msgDateFg);
	_subtitle.drawLeftElided(p, padding.left(), tshift, paintw, width());
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;

	auto heavy = false;
	auto created = false;
	auto &&tasks = ranges::views::zip(
		_tasks,
		ranges::views::ints(0, int(_tasks.size())));
	for (const auto &[task, index] : tasks) {
		const auto was = !task.userpic.null();
		const auto height = paintTask(
			p,
			task,
			padding.left(),
			tshift,
			paintw,
			width(),
			context);
		appendTaskHighlight(task.id, tshift, height, context);
		if (was) {
			heavy = true;
		} else if (!task.userpic.null()) {
			created = true;
		}
		tshift += height;
	}
	if (!heavy && created) {
		history()->owner().registerHeavyViewPart(_parent);
	}
	paintBottom(p, padding.left(), tshift, paintw, context);
}

void TodoList::paintBottom(
		Painter &p,
		int left,
		int top,
		int paintw,
		const PaintContext &context) const {
	const auto stringtop = top
		+ st::msgPadding.bottom()
		+ st::historyPollBottomButtonTop;
	const auto stm = context.messageStyle();

	p.setPen(stm->msgDateFg);
	_completionStatusLabel.draw(p, left, stringtop, paintw, style::al_top);
}

void TodoList::radialAnimationCallback() const {
	if (!anim::Disabled()) {
		repaint();
	}
}

int TodoList::paintTask(
		Painter &p,
		const Task &task,
		int left,
		int top,
		int width,
		int outerWidth,
		const PaintContext &context) const {
	const auto height = countTaskHeight(task, width);
	const auto stm = context.messageStyle();
	const auto aleft = left + st::historyChecklistTaskPadding.left();
	const auto awidth = width
		- st::historyChecklistTaskPadding.left()
		- st::historyChecklistTaskPadding.right();

	if (task.ripple) {
		p.setOpacity(st::historyPollRippleOpacity);
		task.ripple->paint(
			p,
			left - st::msgPadding.left(),
			top,
			outerWidth,
			&stm->msgWaveformInactive->c);
		if (task.ripple->empty()) {
			task.ripple.reset();
		}
		p.setOpacity(1.);
	}

	if (canComplete()) {
		paintRadio(p, task, left, top, context);
	} else {
		paintStatus(p, task, left, top, context);
	}

	top += task.completionDate
		? st::historyChecklistCheckedTop
		: st::historyChecklistTaskPadding.top();
	p.setPen(stm->historyTextFg);
	task.text.draw(p, {
		.position = { aleft, top },
		.availableWidth = awidth,
		.palette = &stm->textPalette,
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
	});
	if (task.completionDate) {
		const auto nameTop = top
			+ height
			- st::historyChecklistTaskPadding.bottom()
			+ st::historyChecklistCheckedTop
			- st::normalFont->height;
		p.setPen(stm->msgDateFg);
		task.name.drawLeft(p, aleft, nameTop, awidth, outerWidth);
	}
	return height;
}

void TodoList::appendTaskHighlight(
		int id,
		int top,
		int height,
		const PaintContext &context) const {
	if (context.highlight.todoItemId != id
		|| context.highlight.collapsion <= 0.) {
		return;
	}
	const auto to = context.highlightInterpolateTo;
	const auto toProgress = (1. - context.highlight.collapsion);
	if (toProgress >= 1.) {
		context.highlightPathCache->addRect(to);
	} else if (toProgress <= 0.) {
		context.highlightPathCache->addRect(0, top, width(), height);
	} else {
		const auto lerp = [=](int from, int to) {
			return from + (to - from) * toProgress;
		};
		context.highlightPathCache->addRect(
			lerp(0, to.x()),
			lerp(top, to.y()),
			lerp(width(), to.width()),
			lerp(height, to.height()));
	}
}

void TodoList::paintRadio(
		Painter &p,
		const Task &task,
		int left,
		int top,
		const PaintContext &context) const {
	top += st::historyChecklistTaskPadding.top();

	const auto stm = context.messageStyle();

	PainterHighQualityEnabler hq(p);
	const auto &radio = st::historyPollRadio;
	const auto over = ClickHandler::showAsActive(task.handler);
	const auto &regular = stm->msgDateFg;

	const auto checkmark = task.selectedAnimation.value(
		task.completionDate ? 1. : 0.);

	const auto o = p.opacity();
	if (checkmark < 1.) {
		p.setBrush(Qt::NoBrush);
		p.setOpacity(o * (over ? st::historyPollRadioOpacityOver : st::historyPollRadioOpacity));
	}

	const auto rect = QRectF(left, top, radio.diameter, radio.diameter).marginsRemoved(QMarginsF(radio.thickness / 2., radio.thickness / 2., radio.thickness / 2., radio.thickness / 2.));
	if (checkmark > 0. && task.completedBy) {
		const auto skip = st::lineWidth;
		const auto userpic = QRect(
			left + (radio.diameter / 2) + skip,
			top + skip,
			radio.diameter - 2 * skip,
			radio.diameter - 2 * skip);
		if (checkmark < 1.) {
			p.save();
			p.setOpacity(checkmark);
			p.translate(QRectF(userpic).center());
			const auto ratio = 0.4 + 0.6 * checkmark;
			p.scale(ratio, ratio);
			p.translate(-QRectF(userpic).center());
		}
		task.completedBy->paintUserpic(
			p,
			task.userpic,
			userpic.left(),
			userpic.top(),
			userpic.width());
		if (checkmark < 1.) {
			p.restore();
		}
	}
	if (checkmark < 1.) {
		auto pen = regular->p;
		pen.setWidth(radio.thickness);
		p.setPen(pen);
		p.drawEllipse(rect);
	}

	if (checkmark > 0.) {
		const auto removeFull = (radio.diameter / 2 - radio.thickness);
		const auto removeNow = removeFull * (1. - checkmark);
		const auto color = stm->msgFileThumbLinkFg;
		auto pen = color->p;
		pen.setWidth(radio.thickness);
		p.setPen(pen);
		p.setBrush(color);
		p.drawEllipse(rect.marginsRemoved({ removeNow, removeNow, removeNow, removeNow }));
		const auto &icon = stm->historyPollChosen;
		icon.paint(p, left + (radio.diameter - icon.width()) / 2, top + (radio.diameter - icon.height()) / 2, width());

		const auto stm = context.messageStyle();
		auto bgpen = stm->msgBg->p;
		bgpen.setWidth(st::lineWidth);
		const auto outline = QRect(left, top, radio.diameter, radio.diameter);
		const auto paintContent = [&](QPainter &p) {
			p.setPen(bgpen);
			p.setBrush(Qt::NoBrush);
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(outline);
		};
		if (usesBubblePattern(context)) {
			const auto add = st::lineWidth * 3;
			const auto target = outline.marginsAdded(
				{ add, add, add, add });
			Ui::PaintPatternBubblePart(
				p,
				context.viewport,
				context.bubblesPattern->pixmap,
				target,
				paintContent,
				_userpicCircleCache);
		} else {
			paintContent(p);
		}
	}

	p.setOpacity(o);
}

void TodoList::paintStatus(
		Painter &p,
		const Task &task,
		int left,
		int top,
		const PaintContext &context) const {
	top += st::historyChecklistTaskPadding.top();

	const auto stm = context.messageStyle();

	const auto &radio = st::historyPollRadio;
	const auto completed = (task.completionDate != 0);

	const auto rect = QRect(left, top, radio.diameter, radio.diameter);
	if (completed) {
		const auto &icon = stm->historyPollChosen;
		icon.paint(
			p,
			left + (radio.diameter - icon.width()) / 2,
			top + (radio.diameter - icon.height()) / 2,
			width(),
			stm->msgFileBg->c);
	} else {
		p.setPen(Qt::NoPen);
		p.setBrush(stm->msgFileBg);

		PainterHighQualityEnabler hq(p);
		p.drawEllipse(style::centerrect(
			rect,
			QRect(0, 0, st::mediaUnreadSize, st::mediaUnreadSize)));
	}
}

TextSelection TodoList::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return _title.adjustSelection(selection, type);
}

uint16 TodoList::fullSelectionLength() const {
	return _title.length();
}

TextForMimeData TodoList::selectedText(TextSelection selection) const {
	return _title.toTextForMimeData(selection);
}

TextState TodoList::textState(QPoint point, StateRequest request) const {
	auto result = TextState(_parent);
	const auto padding = st::msgPadding;
	auto paintw = width();
	auto tshift = st::historyPollQuestionTop;
	if (!isBubbleTop()) {
		tshift -= st::msgFileTopMinus;
	}
	paintw -= padding.left() + padding.right();

	const auto questionH = _title.countHeight(paintw);
	if (QRect(padding.left(), tshift, paintw, questionH).contains(point)) {
		result = TextState(_parent, _title.getState(
			point - QPoint(padding.left(), tshift),
			paintw,
			request.forText()));
		return result;
	}
	const auto aleft = padding.left()
		+ st::historyChecklistTaskPadding.left();
	const auto awidth = paintw
		- st::historyChecklistTaskPadding.left()
		- st::historyChecklistTaskPadding.right();
	tshift += questionH + st::historyPollSubtitleSkip;
	tshift += st::msgDateFont->height + st::historyPollAnswersSkip;
	for (const auto &task : _tasks) {
		const auto height = countTaskHeight(task, paintw);
		if (point.y() >= tshift && point.y() < tshift + height) {
			const auto atop = tshift
				+ (task.completionDate
					? st::historyChecklistCheckedTop
					: st::historyChecklistTaskPadding.top());
			auto taskTextResult = task.text.getState(
				point - QPoint(aleft, atop),
				awidth,
				request.forText());
			if (taskTextResult.link) {
				result.link = taskTextResult.link;
			} else {
				_lastLinkPoint = point;
				result.link = task.handler;
			}
			if (task.completionDate) {
				result.customTooltip = true;
				using Flag = Ui::Text::StateRequest::Flag;
				if (request.flags & Flag::LookupCustomTooltip) {
					result.customTooltipText = langDateTimeFull(
						base::unixtime::parse(task.completionDate));
				}
			}
			return result;
		}
		tshift += height;
	}
	return result;
}

void TodoList::paintBubbleFireworks(
		Painter &p,
		const QRect &bubble,
		crl::time ms) const {
	if (!_fireworksAnimation || _fireworksAnimation->paint(p, bubble)) {
		return;
	}
	_fireworksAnimation = nullptr;
}

void TodoList::clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) {
	if (!handler) return;

	const auto i = ranges::find(
		_tasks,
		handler,
		&Task::handler);
	if (i != end(_tasks)) {
		toggleRipple(*i, pressed);
	}
}

void TodoList::unloadHeavyPart() {
	for (auto &task : _tasks) {
		task.userpic = {};
	}
}

bool TodoList::hasHeavyPart() const {
	for (auto &task : _tasks) {
		if (!task.userpic.null()) {
			return true;
		}
	}
	return false;
}

void TodoList::hideSpoilers() {
	if (_title.hasSpoilers()) {
		_title.setSpoilerRevealed(false, anim::type::instant);
	}
	for (auto &task : _tasks) {
		if (task.text.hasSpoilers()) {
			task.text.setSpoilerRevealed(false, anim::type::instant);
		}
	}
}

std::vector<Media::TodoTaskInfo> TodoList::takeTasksInfo() {
	if (_tasks.empty()) {
		return {};
	}
	return _tasks | ranges::views::transform([](const Task &task) {
		return TodoTaskInfo{
			.id = task.id,
			.completedBy = task.completedBy,
			.completionDate = task.completionDate,
		};
	}) | ranges::to_vector;
}

void TodoList::toggleRipple(Task &task, bool pressed) {
	if (pressed) {
		const auto outerWidth = width();
		const auto innerWidth = outerWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		if (!task.ripple) {
			auto mask = Ui::RippleAnimation::RectMask(QSize(
				outerWidth,
				countTaskHeight(task, innerWidth)));
			task.ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				std::move(mask),
				[=] { repaint(); });
		}
		const auto top = countTaskTop(task, innerWidth);
		task.ripple->add(_lastLinkPoint - QPoint(0, top));
	} else if (task.ripple) {
		task.ripple->lastStop();
	}
}

int TodoList::bottomButtonHeight() const {
	const auto skip = st::historyPollChoiceRight.height()
		- st::historyPollFillingBottom
		- st::historyPollFillingHeight
		- (st::historyPollChoiceRight.height() - st::historyPollFillingHeight) / 2;
	return st::historyPollTotalVotesSkip
		- skip
		+ st::historyPollBottomButtonSkip
		+ st::msgDateFont->height
		+ st::msgPadding.bottom();
}

TodoList::~TodoList() {
	history()->owner().unregisterTodoListView(_todolist, _parent);
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

} // namespace HistoryView

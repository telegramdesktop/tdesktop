/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_suggestions_widget.h"

#include "chat_helpers/emoji_keywords.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "emoji_suggestions_helper.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/emoji_config.h"
#include "ui/ui_utility.h"
#include "ui/cached_round_corners.h"
#include "ui/round_rect.h"
#include "platform/platform_specific.h"
#include "core/application.h"
#include "base/event_filter.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>
#include <QtGui/QTextBlock>

namespace Ui {
namespace Emoji {
namespace {

constexpr auto kShowExactDelay = crl::time(300);
constexpr auto kMaxNonScrolledEmoji = 7;

} // namespace

class SuggestionsWidget final : public Ui::RpWidget {
public:
	SuggestionsWidget(
		QWidget *parent,
		const style::EmojiSuggestions &st,
		not_null<Main::Session*> session,
		bool suggestCustomEmoji,
		Fn<bool(not_null<DocumentData*>)> allowCustomWithoutPremium);
	~SuggestionsWidget();

	void showWithQuery(SuggestionsQuery query, bool force = false);
	void selectFirstResult();
	bool handleKeyEvent(int key);

	[[nodiscard]] rpl::producer<bool> toggleAnimated() const;

	struct Chosen {
		QString emoji;
		QString customData;
	};
	[[nodiscard]] rpl::producer<Chosen> triggered() const;

private:
	struct Row {
		Row(not_null<EmojiPtr> emoji, const QString &replacement);

		Ui::Text::CustomEmoji *custom = nullptr;
		DocumentData *document = nullptr;
		not_null<EmojiPtr> emoji;
		QString replacement;
	};
	struct Custom {
		not_null<DocumentData*> document;
		not_null<EmojiPtr> emoji;
		QString replacement;
	};

	bool eventHook(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	void scrollByWheelEvent(not_null<QWheelEvent*> e);
	void paintFadings(QPainter &p) const;

	[[nodiscard]] std::vector<Row> getRowsByQuery(const QString &text) const;
	[[nodiscard]] base::flat_multi_map<int, Custom> lookupCustom(
		const std::vector<Row> &rows) const;
	[[nodiscard]] std::vector<Row> appendCustom(
		std::vector<Row> rows);
	[[nodiscard]] std::vector<Row> appendCustom(
		std::vector<Row> rows,
		const base::flat_multi_map<int, Custom> &custom);
	void resizeToRows();
	void setSelected(
		int selected,
		anim::type animated = anim::type::instant);
	void setPressed(int pressed);
	void clearMouseSelection();
	void clearSelection();
	void updateSelectedItem();
	void updateItem(int index);
	[[nodiscard]] QRect inner() const;
	[[nodiscard]] QPoint innerShift() const;
	[[nodiscard]] QPoint mapToInner(QPoint globalPosition) const;
	void selectByMouse(QPoint globalPosition);
	bool triggerSelectedRow() const;
	void triggerRow(const Row &row) const;

	[[nodiscard]] int scrollCurrent() const;
	void scrollTo(int value, anim::type animated = anim::type::instant);
	void stopAnimations();

	[[nodiscard]] not_null<Ui::Text::CustomEmoji*> resolveCustomEmoji(
		not_null<DocumentData*> document);
	void customEmojiRepaint();

	const style::EmojiSuggestions &_st;
	const not_null<Main::Session*> _session;
	SuggestionsQuery _query;
	std::vector<Row> _rows;
	bool _suggestCustomEmoji = false;
	Fn<bool(not_null<DocumentData*>)> _allowCustomWithoutPremium;

	Ui::RoundRect _overRect;

	base::flat_map<
		not_null<DocumentData*>,
		std::unique_ptr<Ui::Text::CustomEmoji>> _customEmoji;
	bool _repaintScheduled = false;

	std::optional<QPoint> _lastMousePosition;
	bool _mouseSelection = false;
	int _selected = -1;
	int _pressed = -1;

	int _scrollValue = 0;
	Ui::Animations::Simple _scrollAnimation;
	Ui::Animations::Simple _selectedAnimation;
	int _scrollMax = 0;
	int _oneWidth = 0;
	QMargins _padding;

	QPoint _mousePressPosition;
	int _dragScrollStart = -1;

	rpl::event_stream<bool> _toggleAnimated;
	rpl::event_stream<Chosen> _triggered;

};

SuggestionsWidget::SuggestionsWidget(
	QWidget *parent,
	const style::EmojiSuggestions &st,
	not_null<Main::Session*> session,
	bool suggestCustomEmoji,
	Fn<bool(not_null<DocumentData*>)> allowCustomWithoutPremium)
: RpWidget(parent)
, _st(st)
, _session(session)
, _suggestCustomEmoji(suggestCustomEmoji)
, _allowCustomWithoutPremium(std::move(allowCustomWithoutPremium))
, _overRect(st::roundRadiusSmall, _st.overBg)
, _oneWidth(st::emojiSuggestionSize)
, _padding(st::emojiSuggestionsPadding) {
	resize(
		_oneWidth + _padding.left() + _padding.right(),
		_oneWidth + _padding.top() + _padding.bottom());
	setMouseTracking(true);
}

SuggestionsWidget::~SuggestionsWidget() = default;

rpl::producer<bool> SuggestionsWidget::toggleAnimated() const {
	return _toggleAnimated.events();
}

auto SuggestionsWidget::triggered() const -> rpl::producer<Chosen> {
	return _triggered.events();
}

void SuggestionsWidget::showWithQuery(SuggestionsQuery query, bool force) {
	if (!force && (_query == query)) {
		return;
	}
	_query = query;
	auto rows = [&] {
		if (const auto emoji = std::get_if<EmojiPtr>(&query)) {
			return appendCustom(
				{},
				lookupCustom({ Row(*emoji, (*emoji)->text()) }));
		}
		return appendCustom(getRowsByQuery(v::get<QString>(query)));
	}();
	if (rows.empty()) {
		_toggleAnimated.fire(false);
	}
	clearSelection();
	setPressed(-1);
	_rows = std::move(rows);
	resizeToRows();
	update();

	Ui::PostponeCall(this, [=] {
		if (!_rows.empty()) {
			_toggleAnimated.fire(true);
		}
	});
}

void SuggestionsWidget::selectFirstResult() {
	if (!_rows.empty() && _selected < 0) {
		setSelected(0);
	}
}

auto SuggestionsWidget::appendCustom(std::vector<Row> rows)
-> std::vector<Row> {
	const auto custom = lookupCustom(rows);
	return appendCustom(std::move(rows), custom);
}

auto SuggestionsWidget::lookupCustom(const std::vector<Row> &rows) const
-> base::flat_multi_map<int, Custom> {
	if (rows.empty()
		|| !_suggestCustomEmoji
		|| !Core::App().settings().suggestAnimatedEmoji()) {
		return {};
	}
	auto custom = base::flat_multi_map<int, Custom>();
	const auto premium = _session->premium();
	const auto stickers = &_session->data().stickers();
	for (const auto setId : stickers->emojiSetsOrder()) {
		const auto i = stickers->sets().find(setId);
		if (i == end(stickers->sets())) {
			continue;
		}
		for (const auto &document : i->second->stickers) {
			if (!premium
				&& document->isPremiumEmoji()
				&& (!_allowCustomWithoutPremium
					|| !_allowCustomWithoutPremium(document))) {
				// Skip the whole premium emoji set.
				break;
			}
			if (const auto sticker = document->sticker()) {
				if (const auto emoji = Ui::Emoji::Find(sticker->alt)) {
					const auto original = emoji->original();
					const auto j = ranges::find_if(
						rows,
						[&](const Row &row) {
							return row.emoji->original() == original;
						});
					if (j != end(rows)) {
						custom.emplace(int(j - begin(rows)), Custom{
							.document = document,
							.emoji = emoji,
							.replacement = j->replacement,
						});
					}
				}
			}
		}
	}
	return custom;
}

auto SuggestionsWidget::appendCustom(
	std::vector<Row> rows,
	const base::flat_multi_map<int, Custom> &custom)
-> std::vector<Row> {
	rows.reserve(rows.size() + custom.size());
	for (const auto &[position, one] : custom) {
		rows.push_back(Row(one.emoji, one.replacement));
		rows.back().document = one.document;
		rows.back().custom = resolveCustomEmoji(one.document);
	}
	return rows;
}

not_null<Ui::Text::CustomEmoji*> SuggestionsWidget::resolveCustomEmoji(
		not_null<DocumentData*> document) {
	const auto i = _customEmoji.find(document);
	if (i != end(_customEmoji)) {
		return i->second.get();
	}
	auto emoji = document->session().data().customEmojiManager().create(
		document,
		[=] { customEmojiRepaint(); },
		Data::CustomEmojiManager::SizeTag::Large);
	return _customEmoji.emplace(
		document,
		std::move(emoji)
	).first->second.get();
}

void SuggestionsWidget::customEmojiRepaint() {
	if (_repaintScheduled) {
		return;
	}
	_repaintScheduled = true;
	update();
}

SuggestionsWidget::Row::Row(
	not_null<EmojiPtr> emoji,
	const QString &replacement)
: emoji(emoji)
, replacement(replacement) {
}

auto SuggestionsWidget::getRowsByQuery(const QString &text) const
-> std::vector<Row> {
	if (text.isEmpty()) {
		return {};
	}
	const auto middle = (text[0] == ':');
	const auto real = middle ? text.mid(1) : text;
	const auto simple = [&] {
		if (!middle || text.size() > 2) {
			return false;
		}
		// Suggest :D and :-P only as exact matches.
		return ranges::none_of(text, [](QChar ch) { return ch.isLower(); });
	}();
	const auto exact = !middle || simple;
	const auto list = Core::App().emojiKeywords().queryMine(real, exact);
	using Entry = ChatHelpers::EmojiKeywords::Result;
	return ranges::views::all(
		list
	) | ranges::views::transform([](const Entry &result) {
		return Row(result.emoji, result.replacement);
	}) | ranges::to_vector;
}

void SuggestionsWidget::resizeToRows() {
	const auto count = int(_rows.size());
	const auto scrolled = (count > kMaxNonScrolledEmoji);
	const auto fullWidth = count * _oneWidth;
	const auto newWidth = scrolled
		? st::emojiSuggestionsScrolledWidth
		: fullWidth;
	_scrollMax = std::max(0, fullWidth - newWidth);
	if (_scrollValue > _scrollMax || scrollCurrent() > _scrollMax) {
		scrollTo(std::min(_scrollValue, _scrollMax));
	}
	resize(_padding.left() + newWidth + _padding.right(), height());
	update();
}

bool SuggestionsWidget::eventHook(QEvent *e) {
	if (e->type() == QEvent::Wheel) {
		selectByMouse(QCursor::pos());
		if (_selected >= 0 && _pressed < 0) {
			scrollByWheelEvent(static_cast<QWheelEvent*>(e));
		}
	}
	return RpWidget::eventHook(e);
}

void SuggestionsWidget::scrollByWheelEvent(not_null<QWheelEvent*> e) {
	const auto horizontal = (e->angleDelta().x() != 0);
	const auto vertical = (e->angleDelta().y() != 0);
	const auto current = scrollCurrent();
	const auto scroll = [&] {
		if (horizontal) {
			const auto delta = e->pixelDelta().x()
				? e->pixelDelta().x()
				: e->angleDelta().x();
			return std::clamp(
				current - ((rtl() ? -1 : 1) * delta),
				0,
				_scrollMax);
		} else if (vertical) {
			const auto delta = e->pixelDelta().y()
				? e->pixelDelta().y()
				: e->angleDelta().y();
			return std::clamp(current - delta, 0, _scrollMax);
		}
		return current;
	}();
	if (current != scroll) {
		scrollTo(scroll);
		if (!_lastMousePosition) {
			_lastMousePosition = QCursor::pos();
		}
		selectByMouse(*_lastMousePosition);
		update();
	}
}

void SuggestionsWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	_repaintScheduled = false;

	const auto clip = e->rect();
	p.fillRect(clip, _st.bg);

	const auto shift = innerShift();
	p.translate(-shift);
	const auto paint = clip.translated(shift);
	const auto from = std::max(paint.x(), 0) / _oneWidth;
	const auto till = std::min(
		(paint.x() + paint.width() + _oneWidth - 1) / _oneWidth,
		int(_rows.size()));

	const auto selected = (_pressed >= 0)
		? _pressed
		: _selectedAnimation.value(_selected);
	if (selected > -1.) {
		_overRect.paint(
			p,
			QRect(selected * _oneWidth, 0, _oneWidth, _oneWidth));
	}

	auto context = Ui::CustomEmoji::Context{
		.textColor = _st.textFg->c,
		.now = crl::now(),
	};
	for (auto i = from; i != till; ++i) {
		const auto &row = _rows[i];
		const auto emoji = row.emoji;
		const auto esize = Ui::Emoji::GetSizeLarge();
		const auto size = esize / style::DevicePixelRatio();
		const auto x = i * _oneWidth + (_oneWidth - size) / 2;
		const auto y = (_oneWidth - size) / 2;
		if (row.custom) {
			context.position = { x, y };
			row.custom->paint(p, context);
		} else {
			Ui::Emoji::Draw(p, emoji, esize, x, y);
		}
	}
	paintFadings(p);
}

void SuggestionsWidget::paintFadings(QPainter &p) const {
	const auto scroll = scrollCurrent();
	const auto o_left = std::clamp(
		scroll / float64(st::emojiSuggestionsFadeAfter),
		0.,
		1.);
	const auto shift = innerShift();
	if (o_left > 0.) {
		p.setOpacity(o_left);
		const auto rect = myrtlrect(
			shift.x(),
			0,
			_st.fadeLeft.width(),
			height());
		_st.fadeLeft.fill(p, rect);
		p.setOpacity(1.);
	}
	const auto o_right = std::clamp(
		(_scrollMax - scroll) / float64(st::emojiSuggestionsFadeAfter),
		0.,
		1.);
	if (o_right > 0.) {
		p.setOpacity(o_right);
		const auto rect = myrtlrect(
			shift.x() + width() - _st.fadeRight.width(),
			0,
			_st.fadeRight.width(),
			height());
		_st.fadeRight.fill(p, rect);
		p.setOpacity(1.);
	}
}

void SuggestionsWidget::keyPressEvent(QKeyEvent *e) {
	handleKeyEvent(e->key());
}

bool SuggestionsWidget::handleKeyEvent(int key) {
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		return triggerSelectedRow();
	} else if (key == Qt::Key_Tab) {
		if (_selected < 0 || _selected >= _rows.size()) {
			setSelected(0);
		}
		return triggerSelectedRow();
	} else if (_rows.empty()
		|| (key != Qt::Key_Up
			&& key != Qt::Key_Down
			&& key != Qt::Key_Left
			&& key != Qt::Key_Right)) {
		return false;
	}

	const auto delta = (key == Qt::Key_Down || key == Qt::Key_Right)
		? 1
		: -1;
	if (delta < 0 && _selected < 0) {
		return false;
	}
	auto start = _selected;
	if (start < 0 || start >= _rows.size()) {
		start = (delta > 0) ? (_rows.size() - 1) : 0;
	}
	auto newSelected = start + delta;
	if (newSelected < 0) {
		newSelected = -1;
	} else if (newSelected >= _rows.size()) {
		newSelected -= _rows.size();
	}

	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	setSelected(newSelected, anim::type::normal);
	return true;
}

void SuggestionsWidget::setSelected(int selected, anim::type animated) {
	if (selected >= _rows.size()) {
		selected = -1;
	}
	if (animated == anim::type::normal) {
		_selectedAnimation.start(
			[=] { update(); },
			_selected,
			selected,
			st::universalDuration,
			anim::sineInOut);
		if (_scrollMax > 0) {
			const auto selectedMax = int(_rows.size()) - 3;
			const auto selectedForScroll = std::min(
				std::max(selected, 1) - 1,
				selectedMax);
			scrollTo((_scrollMax * selectedForScroll) / selectedMax, animated);
		}
	} else if (_selectedAnimation.animating()) {
		_selectedAnimation.stop();
		update();
	}
	if (_selected != selected) {
		updateSelectedItem();
		_selected = selected;
		updateSelectedItem();
	}
}

int SuggestionsWidget::scrollCurrent() const {
	return _scrollAnimation.value(_scrollValue);
}

void SuggestionsWidget::scrollTo(int value, anim::type animated) {
	if (animated == anim::type::instant) {
		_scrollAnimation.stop();
	} else {
		_scrollAnimation.start(
			[=] { update(); },
			_scrollValue,
			value,
			st::universalDuration,
			anim::sineInOut);
	}
	_scrollValue = value;
	update();
}

void SuggestionsWidget::stopAnimations() {
	_scrollValue = _scrollAnimation.value(_scrollValue);
	_scrollAnimation.stop();
}

void SuggestionsWidget::setPressed(int pressed) {
	if (pressed >= _rows.size()) {
		pressed = -1;
	}
	if (_pressed != pressed) {
		_pressed = pressed;
		if (_pressed >= 0) {
			_mousePressPosition = QCursor::pos();
		}
	}
}

void SuggestionsWidget::clearMouseSelection() {
	if (_mouseSelection) {
		clearSelection();
	}
}

void SuggestionsWidget::clearSelection() {
	_mouseSelection = false;
	_lastMousePosition = std::nullopt;
	setSelected(-1);
}

void SuggestionsWidget::updateItem(int index) {
	if (index >= 0 && index < _rows.size()) {
		update(
			_padding.left() + index * _oneWidth - scrollCurrent(),
			_padding.top(),
			_oneWidth,
			_oneWidth);
	}
}

void SuggestionsWidget::updateSelectedItem() {
	updateItem(_selected);
}

QRect SuggestionsWidget::inner() const {
	return QRect(0, 0, _rows.size() * _oneWidth, _oneWidth);
}

QPoint SuggestionsWidget::innerShift() const {
	return QPoint(scrollCurrent() - _padding.left(), -_padding.top());
}

QPoint SuggestionsWidget::mapToInner(QPoint globalPosition) const {
	return mapFromGlobal(globalPosition) + innerShift();
}

void SuggestionsWidget::mouseMoveEvent(QMouseEvent *e) {
	const auto globalPosition = e->globalPos();
	if (_dragScrollStart >= 0) {
		const auto delta = (_mousePressPosition.x() - globalPosition.x());
		const auto scroll = std::clamp(
			_dragScrollStart + (rtl() ? -1 : 1) * delta,
			0,
			_scrollMax);
		if (scrollCurrent() != scroll) {
			scrollTo(scroll);
			update();
		}
		return;
	} else if ((_pressed >= 0)
		&& (_scrollMax > 0)
		&& ((_mousePressPosition - globalPosition).manhattanLength()
			>= QApplication::startDragDistance())) {
		_dragScrollStart = scrollCurrent();
		_mousePressPosition = globalPosition;
		scrollTo(_dragScrollStart);
	}
	if (inner().contains(mapToInner(globalPosition))) {
		if (!_lastMousePosition) {
			_lastMousePosition = globalPosition;
			return;
		} else if (!_mouseSelection
			&& *_lastMousePosition == globalPosition) {
			return;
		}
		selectByMouse(globalPosition);
	} else {
		clearMouseSelection();
	}
}

void SuggestionsWidget::selectByMouse(QPoint globalPosition) {
	_mouseSelection = true;
	_lastMousePosition = globalPosition;
	const auto p = mapToInner(globalPosition);
	const auto selected = (p.x() >= 0) ? (p.x() / _oneWidth) : -1;
	setSelected((selected >= 0 && selected < _rows.size()) ? selected : -1);
}

void SuggestionsWidget::mousePressEvent(QMouseEvent *e) {
	selectByMouse(e->globalPos());
	if (_selected >= 0) {
		setPressed(_selected);
	}
}

void SuggestionsWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_pressed >= 0) {
		const auto pressed = _pressed;
		setPressed(-1);
		if (_dragScrollStart >= 0) {
			_dragScrollStart = -1;
		} else if (pressed == _selected) {
			triggerRow(_rows[_selected]);
		}
	}
}

bool SuggestionsWidget::triggerSelectedRow() const {
	if (_selected >= 0) {
		triggerRow(_rows[_selected]);
		return true;
	}
	return false;
}

void SuggestionsWidget::triggerRow(const Row &row) const {
	_triggered.fire({
		row.emoji->text(),
		(row.document
			? Data::SerializeCustomEmojiId(row.document)
			: QString()),
	});
}

void SuggestionsWidget::enterEventHook(QEnterEvent *e) {
	if (!inner().contains(mapToInner(QCursor::pos()))) {
		clearMouseSelection();
	}
	return TWidget::enterEventHook(e);
}

void SuggestionsWidget::leaveEventHook(QEvent *e) {
	clearMouseSelection();
	return TWidget::leaveEventHook(e);
}

SuggestionsController::SuggestionsController(
	not_null<QWidget*> outer,
	not_null<QTextEdit*> field,
	not_null<Main::Session*> session,
	const Options &options)
: _st(options.st ? *options.st : st::defaultEmojiSuggestions)
, _field(field)
, _session(session)
, _showExactTimer([=] { showWithQuery(getEmojiQuery()); })
, _options(options) {
	_container = base::make_unique_q<InnerDropdown>(outer, _st.dropdown);
	_container->setAutoHiding(false);
	_suggestions = _container->setOwnedWidget(
		object_ptr<Ui::Emoji::SuggestionsWidget>(
			_container,
			_st,
			session,
			_options.suggestCustomEmoji,
			_options.allowCustomWithoutPremium));

	setReplaceCallback(nullptr);

	const auto fieldCallback = [=](not_null<QEvent*> event) {
		return (_container && fieldFilter(event))
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	};
	_fieldFilter.reset(base::install_event_filter(_field, fieldCallback));

	const auto outerCallback = [=](not_null<QEvent*> event) {
		return (_container && outerFilter(event))
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	};
	_outerFilter.reset(base::install_event_filter(outer, outerCallback));

	QObject::connect(
		_field,
		&QTextEdit::textChanged,
		_container,
		[=] { handleTextChange(); });
	QObject::connect(
		_field,
		&QTextEdit::cursorPositionChanged,
		_container,
		[=] { handleCursorPositionChange(); });

	_suggestions->toggleAnimated(
	) | rpl::start_with_next([=](bool visible) {
		suggestionsUpdated(visible);
	}, _lifetime);
	_suggestions->triggered(
	) | rpl::start_with_next([=](const SuggestionsWidget::Chosen &chosen) {
		replaceCurrent(chosen.emoji, chosen.customData);
	}, _lifetime);
	Core::App().emojiKeywords().refreshed(
	) | rpl::start_with_next([=] {
		_keywordsRefreshed = true;
		if (!_showExactTimer.isActive()) {
			showWithQuery(_lastShownQuery);
		}
	}, _lifetime);

	updateForceHidden();

	_container->shownValue(
	) | rpl::filter([=](bool shown) {
		return shown && !_shown;
	}) | rpl::start_with_next([=] {
		_container->hide();
	}, _container->lifetime());

	handleTextChange();
}

not_null<SuggestionsController*> SuggestionsController::Init(
		not_null<QWidget*> outer,
		not_null<Ui::InputField*> field,
		not_null<Main::Session*> session,
		const Options &options) {
	const auto result = Ui::CreateChild<SuggestionsController>(
		field.get(),
		outer,
		field->rawTextEdit(),
		session,
		options);
	result->setReplaceCallback([=](
			int from,
			int till,
			const QString &replacement,
			const QString &customEmojiData) {
		field->commitInstantReplacement(
			from,
			till,
			replacement,
			customEmojiData);
	});
	return result;
}

void SuggestionsController::setReplaceCallback(
	Fn<void(
		int from,
		int till,
		const QString &replacement,
		const QString &customEmojiData)> callback) {
	if (callback) {
		_replaceCallback = std::move(callback);
	} else {
		_replaceCallback = [=](
				int from,
				int till,
				const QString &replacement,
				const QString &customEmojiData) {
			auto cursor = _field->textCursor();
			cursor.setPosition(from);
			cursor.setPosition(till, QTextCursor::KeepAnchor);
			cursor.insertText(replacement);
		};
	}
}

void SuggestionsController::handleTextChange() {
	if (Core::App().settings().suggestEmoji()
		&& _field->textCursor().position() > 0) {
		Core::App().emojiKeywords().refresh();
	}

	_ignoreCursorPositionChange = true;
	InvokeQueued(_container, [=] { _ignoreCursorPositionChange = false; });

	const auto query = getEmojiQuery();
	if (v::is<EmojiPtr>(query)) {
		showWithQuery(query);
		InvokeQueued(_container, [=] {
			if (_shown) {
				updateGeometry();
			}
		});
		return;
	}
	const auto text = v::get<QString>(query);
	if (text.isEmpty() || _textChangeAfterKeyPress) {
		const auto exact = !text.isEmpty() && (text[0] != ':');
		if (exact) {
			const auto hidden = _container->isHidden()
				|| _container->isHiding();
			_showExactTimer.callOnce(hidden ? kShowExactDelay : 0);
		} else {
			showWithQuery(query);
			_suggestions->selectFirstResult();
		}
	}
}

void SuggestionsController::showWithQuery(SuggestionsQuery query) {
	_showExactTimer.cancel();
	const auto force = base::take(_keywordsRefreshed);
	_lastShownQuery = query;
	_suggestions->showWithQuery(_lastShownQuery, force);
	_container->resizeToContent();
}

SuggestionsQuery SuggestionsController::getEmojiQuery() {
	if (!Core::App().settings().suggestEmoji()) {
		return QString();
	}

	const auto cursor = _field->textCursor();
	if (cursor.hasSelection()) {
		return QString();
	}

	const auto modernLimit = Core::App().emojiKeywords().maxQueryLength();
	const auto legacyLimit = GetSuggestionMaxLength();
	const auto position = cursor.position();
	const auto findTextPart = [&]() -> SuggestionsQuery {
		auto previousFragmentStart = 0;
		auto previousFragmentName = QString();
		auto document = _field->document();
		auto block = document->findBlock(position);
		for (auto i = block.begin(); !i.atEnd(); ++i) {
			auto fragment = i.fragment();
			if (!fragment.isValid()) {
				continue;
			}

			auto from = fragment.position();
			auto till = from + fragment.length();
			const auto format = fragment.charFormat();
			if (format.objectType() == InputField::kCustomEmojiFormat) {
				previousFragmentName = QString();
				continue;
			} else if (format.isImageFormat()) {
				const auto imageName = format.toImageFormat().name();
				if (from >= position || till < position) {
					previousFragmentStart = from;
					previousFragmentName = imageName;
					continue;
				} else if (const auto emoji = Emoji::FromUrl(imageName)) {
					_queryStartPosition = position - 1;
					const auto start = (previousFragmentName == imageName)
						? previousFragmentStart
						: from;
					_emojiQueryLength = (position - start);
					return emoji;
				} else {
					continue;
				}
			}
			if (from >= position || till < position) {
				previousFragmentName = QString();
				continue;
			}
			_queryStartPosition = from;
			_emojiQueryLength = 0;
			return fragment.text();
		}
		return QString();
	};

	const auto part = findTextPart();
	if (const auto emoji = std::get_if<EmojiPtr>(&part)) {
		return *emoji;
	}
	const auto text = v::get<QString>(part);
	if (text.isEmpty()) {
		return QString();
	}
	const auto length = position - _queryStartPosition;
	for (auto i = length; i != 0;) {
		if (text[--i] == ':') {
			const auto previous = (i > 0) ? text[i - 1] : QChar(0);
			if (i > 0 && (previous.isLetter() || previous.isDigit())) {
				return QString();
			} else if (i + 1 == length || text[i + 1].isSpace()) {
				return QString();
			}
			_queryStartPosition += i + 2;
			return text.mid(i, length - i);
		}
		if (length - i > legacyLimit && length - i > modernLimit) {
			return QString();
		}
	}

	// Exact query should be full input field value.
	const auto end = [&] {
		auto cursor = _field->textCursor();
		cursor.movePosition(QTextCursor::End);
		return cursor.position();
	}();
	if (!_options.suggestExactFirstWord
		|| !length
		|| text[0].isSpace()
		|| (length > modernLimit)
		|| (_queryStartPosition != 0)
		|| (position != end)) {
		return QString();
	}
	return text;
}

void SuggestionsController::replaceCurrent(
		const QString &replacement,
		const QString &customEmojiData) {
	const auto cursor = _field->textCursor();
	const auto position = cursor.position();
	const auto suggestion = getEmojiQuery();
	if (v::is<EmojiPtr>(suggestion)) {
		const auto weak = Ui::MakeWeak(_container.get());
		const auto count = std::max(_emojiQueryLength, 1);
		for (auto i = 0; i != count; ++i) {
			const auto start = position - count + i;
			_replaceCallback(start, start + 1, replacement, customEmojiData);
			if (!weak) {
				return;
			}
		}
	} else if (v::get<QString>(suggestion).isEmpty()) {
		showWithQuery(QString());
	} else {
		const auto from = position - v::get<QString>(suggestion).size();
		_replaceCallback(from, position, replacement, customEmojiData);
	}
}

void SuggestionsController::handleCursorPositionChange() {
	InvokeQueued(_container, [=] {
		if (_ignoreCursorPositionChange) {
			return;
		}
		showWithQuery(QString());
	});
}

void SuggestionsController::suggestionsUpdated(bool visible) {
	_shown = visible;
	if (_shown) {
		_container->resizeToContent();
		updateGeometry();
		if (!_forceHidden) {
			if (_container->isHidden() || _container->isHiding()) {
				raise();
			}
			_container->showAnimated(
				Ui::PanelAnimation::Origin::BottomLeft);
		}
	} else if (!_forceHidden) {
		_container->hideAnimated();
	}
}

void SuggestionsController::updateGeometry() {
	auto cursor = _field->textCursor();
	cursor.setPosition(_queryStartPosition);
	auto aroundRect = _field->cursorRect(cursor);
	aroundRect.setTopLeft(_field->viewport()->mapToGlobal(aroundRect.topLeft()));
	aroundRect.setTopLeft(_container->parentWidget()->mapFromGlobal(aroundRect.topLeft()));
	auto boundingRect = _container->parentWidget()->rect();
	auto origin = rtl() ? PanelAnimation::Origin::BottomRight : PanelAnimation::Origin::BottomLeft;
	auto point = rtl() ? (aroundRect.topLeft() + QPoint(aroundRect.width(), 0)) : aroundRect.topLeft();
	const auto padding = _st.dropdown.padding;
	const auto shift = std::min(_container->width() - padding.left() - padding.right(), st::emojiSuggestionSize) / 2;
	point -= rtl() ? QPoint(_container->width() - padding.right() - shift, _container->height()) : QPoint(padding.left() + shift, _container->height());
	if (rtl()) {
		if (point.x() < boundingRect.x()) {
			point.setX(boundingRect.x());
		}
		if (point.x() + _container->width() > boundingRect.x() + boundingRect.width()) {
			point.setX(boundingRect.x() + boundingRect.width() - _container->width());
		}
	} else {
		if (point.x() + _container->width() > boundingRect.x() + boundingRect.width()) {
			point.setX(boundingRect.x() + boundingRect.width() - _container->width());
		}
		if (point.x() < boundingRect.x()) {
			point.setX(boundingRect.x());
		}
	}
	if (point.y() < boundingRect.y()) {
		point.setY(aroundRect.y() + aroundRect.height());
		origin = (origin == PanelAnimation::Origin::BottomRight) ? PanelAnimation::Origin::TopRight : PanelAnimation::Origin::TopLeft;
	}
	_container->move(point);
}

void SuggestionsController::updateForceHidden() {
	_forceHidden = !_field->isVisible() || !_field->hasFocus();
	if (_forceHidden) {
		_container->hideFast();
	} else if (_shown) {
		_container->showFast();
	}
}

bool SuggestionsController::fieldFilter(not_null<QEvent*> event) {
	auto type = event->type();
	switch (type) {
	case QEvent::Move:
	case QEvent::Resize: {
		if (_shown) {
			updateGeometry();
		}
	} break;

	case QEvent::Show:
	case QEvent::ShowToParent:
	case QEvent::Hide:
	case QEvent::HideToParent:
	case QEvent::FocusIn:
	case QEvent::FocusOut: {
		updateForceHidden();
	} break;

	case QEvent::KeyPress: {
		const auto key = static_cast<QKeyEvent*>(event.get())->key();
		switch (key) {
		case Qt::Key_Enter:
		case Qt::Key_Return:
		case Qt::Key_Tab:
		case Qt::Key_Up:
		case Qt::Key_Down:
		case Qt::Key_Left:
		case Qt::Key_Right:
			if (_shown && !_forceHidden) {
				return _suggestions->handleKeyEvent(key);
			}
			break;

		case Qt::Key_Escape:
			if (_shown && !_forceHidden) {
				showWithQuery(QString());
				return true;
			}
			break;
		}
		_textChangeAfterKeyPress = true;
		InvokeQueued(_container, [=] { _textChangeAfterKeyPress = false; });
	} break;
	}
	return false;
}

bool SuggestionsController::outerFilter(not_null<QEvent*> event) {
	auto type = event->type();
	switch (type) {
	case QEvent::Move:
	case QEvent::Resize: {
		// updateGeometry uses not only container geometry, but also
		// container children geometries that will be updated later.
		InvokeQueued(_container, [=] {
			if (_shown) {
				updateGeometry();
			}
		});
	} break;
	}
	return false;
}

void SuggestionsController::raise() {
	_container->raise();
}

} // namespace Emoji
} // namespace Ui

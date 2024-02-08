/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/tabbed_search.h"

#include "base/qt_signal_producer.h"
#include "lang/lang_keys.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

constexpr auto kDebounceTimeout = crl::time(400);
constexpr auto kCategoryIconSizeOverride = 22;

class GroupsStrip final : public RpWidget {
public:
	GroupsStrip(
		QWidget *parent,
		const style::TabbedSearch &st,
		rpl::producer<std::vector<EmojiGroup>> groups,
		Text::CustomEmojiFactory factory);

	void scrollByWheel(QWheelEvent *e);

	struct Chosen {
		not_null<const EmojiGroup*> group;
		int iconLeft = 0;
		int iconRight = 0;
	};
	[[nodiscard]] rpl::producer<Chosen> chosen() const;
	void clearChosen();

	[[nodiscard]] rpl::producer<int> moveRequests() const;

private:
	struct Button {
		EmojiGroup group;
		QString iconId;
		std::unique_ptr<Text::CustomEmoji> icon;
	};

	void init(rpl::producer<std::vector<EmojiGroup>> groups);
	void set(std::vector<EmojiGroup> list);

	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void fireChosenGroup();

	static inline auto FindById(auto &&buttons, QStringView id) {
		return ranges::find(buttons, id, &Button::iconId);
	}

	const style::TabbedSearch &_st;
	const Text::CustomEmojiFactory _factory;

	std::vector<Button> _buttons;
	rpl::event_stream<Chosen> _chosenGroup;
	rpl::event_stream<int> _moveRequests;
	QPoint _globalPressPoint, _globalLastPoint;
	bool _dragging = false;
	int _pressed = -1;
	int _chosen = -1;

};

[[nodiscard]] std::vector<QString> FieldQuery(not_null<InputField*> field) {
	if (const auto last = field->getLastText(); !last.isEmpty()) {
		return { last };
	}
	return {};
}

GroupsStrip::GroupsStrip(
	QWidget *parent,
	const style::TabbedSearch &st,
	rpl::producer<std::vector<EmojiGroup>> groups,
	Text::CustomEmojiFactory factory)
: RpWidget(parent)
, _st(st)
, _factory(std::move(factory)) {
	init(std::move(groups));
}

rpl::producer<GroupsStrip::Chosen> GroupsStrip::chosen() const {
	return _chosenGroup.events();
}

rpl::producer<int> GroupsStrip::moveRequests() const {
	return _moveRequests.events();
}

void GroupsStrip::clearChosen() {
	if (const auto chosen = std::exchange(_chosen, -1); chosen >= 0) {
		update();
	}
}

void GroupsStrip::init(rpl::producer<std::vector<EmojiGroup>> groups) {
	std::move(
		groups
	) | rpl::start_with_next([=](std::vector<EmojiGroup> &&list) {
		set(std::move(list));
	}, lifetime());

	setCursor(style::cur_pointer);
}

void GroupsStrip::set(std::vector<EmojiGroup> list) {
	const auto chosen = (_chosen >= 0)
		? _buttons[_chosen].group.iconId
		: QString();
	auto existing = std::move(_buttons);
	const auto updater = [=](const QString &iconId) {
		return [=] {
			const auto i = FindById(_buttons, iconId);
			if (i != end(_buttons)) {
				const auto index = i - begin(_buttons);
				const auto single = _st.groupWidth;
				update(index * single, 0, single, height());
			}
		};
	};
	for (auto &group : list) {
		const auto i = FindById(existing, group.iconId);
		if (i != end(existing)) {
			_buttons.push_back(std::move(*i));
			existing.erase(i);
		} else {
			const auto loopCount = 1;
			const auto stopAtLastFrame = true;
			_buttons.push_back({
				.iconId = group.iconId,
				.icon = std::make_unique<Text::LimitedLoopsEmoji>(
					_factory(
						group.iconId,
						updater(group.iconId)),
					loopCount,
					stopAtLastFrame),
			});
		}
		_buttons.back().group = std::move(group);
	}
	resize(_buttons.size() * _st.groupWidth, height());
	if (!chosen.isEmpty()) {
		const auto i = FindById(_buttons, chosen);
		if (i != end(_buttons)) {
			_chosen = (i - begin(_buttons));
			fireChosenGroup();
		} else {
			_chosen = -1;
		}
	}
	update();
}

void GroupsStrip::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto index = 0;
	const auto single = _st.groupWidth;
	const auto skip = _st.groupSkip;
	const auto height = this->height();
	const auto clip = e->rect();
	const auto now = crl::now();
	for (const auto &button : _buttons) {
		const auto left = index * single;
		const auto top = 0;
		const auto size = SearchWithGroups::IconSizeOverride();
		if (_chosen == index) {
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(_st.bgActive);
			p.drawEllipse(
				left + skip,
				top + (height - single) / 2 + skip,
				single - 2 * skip,
				single - 2 * skip);
		}
		if (QRect(left, top, single, height).intersects(clip)) {
			button.icon->paint(p, {
				.textColor = (_chosen == index ? _st.fgActive : _st.fg)->c,
				.now = now,
				.position = QPoint(left, top) + QPoint(
					(single - size) / 2,
					(height - size) / 2),
				});
		}
		++index;
	}
}

void GroupsStrip::scrollByWheel(QWheelEvent *e) {
	auto horizontal = (e->angleDelta().x() != 0);
	auto vertical = (e->angleDelta().y() != 0);
	if (!horizontal && !vertical) {
		return;
	}
	const auto delta = horizontal
		? ((style::RightToLeft() ? -1 : 1) * (e->pixelDelta().x()
			? e->pixelDelta().x()
			: e->angleDelta().x()))
		: (e->pixelDelta().y()
			? e->pixelDelta().y()
			: e->angleDelta().y());
	_moveRequests.fire_copy(delta);
}

void GroupsStrip::mouseMoveEvent(QMouseEvent *e) {
	const auto point = e->globalPos();
	if (!_dragging) {
		const auto distance = (point - _globalPressPoint).manhattanLength();
		if (distance >= QApplication::startDragDistance()) {
			_dragging = true;
			_globalLastPoint = _globalPressPoint;
		}
	}
	if (_dragging) {
		const auto delta = (point - _globalLastPoint).x();
		_globalLastPoint = point;
		_moveRequests.fire_copy(delta);
	}
}

void GroupsStrip::mousePressEvent(QMouseEvent *e) {
	const auto index = e->pos().x() / _st.groupWidth;
	const auto chosen = (index < 0 || index >= _buttons.size())
		? -1
		: index;
	_pressed = chosen;
	_globalPressPoint = e->globalPos();
}

void GroupsStrip::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (_dragging) {
		_dragging = false;
		return;
	}
	const auto index = e->pos().x() / _st.groupWidth;
	const auto chosen = (index < 0 || index >= _buttons.size())
		? -1
		: index;
	if (pressed == chosen && chosen >= 0) {
		_chosen = pressed;
		fireChosenGroup();
		update();
	}
}

void GroupsStrip::fireChosenGroup() {
	Expects(_chosen >= 0 && _chosen < _buttons.size());

	_chosenGroup.fire({
		.group = &_buttons[_chosen].group,
		.iconLeft = _chosen * _st.groupWidth,
		.iconRight = (_chosen + 1) * _st.groupWidth,
	});
}

} // namespace

SearchWithGroups::SearchWithGroups(
	QWidget *parent,
	SearchDescriptor descriptor)
: RpWidget(parent)
, _st(descriptor.st)
, _search(CreateChild<FadeWrapScaled<IconButton>>(
	this,
	object_ptr<IconButton>(this, _st.search)))
, _back(CreateChild<FadeWrapScaled<IconButton>>(
	this,
	object_ptr<IconButton>(this, _st.back)))
, _cancel(CreateChild<CrossButton>(this, _st.cancel))
, _field(CreateChild<InputField>(this, _st.field, tr::lng_dlg_filter()))
, _groups(CreateChild<FadeWrapScaled<RpWidget>>(
	this,
	object_ptr<GroupsStrip>(
		this,
		_st,
		std::move(descriptor.groups),
		std::move(descriptor.customEmojiFactory))))
, _fade(CreateChild<RpWidget>(this))
, _debounceTimer([=] { _debouncedQuery = _query.current(); }) {
	initField();
	initGroups();
	initButtons();
	initEdges();
	_inited = true;
}

anim::type SearchWithGroups::animated() const {
	return _inited ? anim::type::normal : anim::type::instant;
}

void SearchWithGroups::initField() {
	_field->changes(
	) | rpl::start_with_next([=] {
		const auto last = FieldQuery(_field);
		_query = last;
		const auto empty = last.empty();
		_fieldEmpty = empty;
		if (empty) {
			_debounceTimer.cancel();
			_debouncedQuery = last;
		} else {
			_debounceTimer.callOnce(kDebounceTimeout);
			_chosenGroup = QString();
			scrollGroupsToStart();
		}
	}, _field->lifetime());

	_fieldPlaceholderWidth = tr::lng_dlg_filter(
	) | rpl::map([=](const QString &value) {
		return _st.field.placeholderFont->width(value);
	}) | rpl::after_next([=] {
		resizeToWidth(width());
	});

	const auto last = FieldQuery(_field);
	_query = last;
	_debouncedQuery = last;
	_fieldEmpty = last.empty();
	_fieldEmpty.value(
	) | rpl::start_with_next([=](bool empty) {
		_cancel->toggle(!empty, animated());
		_groups->toggle(empty, animated());
		resizeToWidth(width());
	}, lifetime());
}

void SearchWithGroups::initGroups() {
	const auto widget = static_cast<GroupsStrip*>(_groups->entity());

	const auto &search = _st.search;
	_fadeLeftStart = search.iconPosition.x() + search.icon.width();
	_groups->move(_fadeLeftStart + _st.defaultFieldWidth, 0);
	widget->resize(widget->width(), _st.height);
	widget->widthValue(
	) | rpl::filter([=] {
		return (width() > 0);
	}) | rpl::start_with_next([=] {
		resizeToWidth(width());
	}, widget->lifetime());

	widget->chosen(
	) | rpl::start_with_next([=](const GroupsStrip::Chosen &chosen) {
		_chosenGroup = chosen.group->iconId;
		_query = chosen.group->emoticons;
		_debouncedQuery = chosen.group->emoticons;
		_debounceTimer.cancel();
		scrollGroupsToIcon(chosen.iconLeft, chosen.iconRight);
	}, lifetime());

	widget->moveRequests(
	) | rpl::start_with_next([=](int delta) {
		moveGroupsBy(width(), delta);
	}, lifetime());

	_chosenGroup.value(
	) | rpl::map([=](const QString &id) {
		return id.isEmpty();
	}) | rpl::start_with_next([=](bool empty) {
		_search->toggle(empty, animated());
		_back->toggle(!empty, animated());
		if (empty) {
			widget->clearChosen();
			if (_field->getLastText().isEmpty()) {
				_query = {};
				_debouncedQuery = {};
				_debounceTimer.cancel();
			}
		} else {
			_field->setText({});
		}
	}, lifetime());
}

void SearchWithGroups::scrollGroupsToIcon(int iconLeft, int iconRight) {
	const auto single = _st.groupWidth;
	const auto fadeRight = _fadeLeftStart + _st.fadeLeft.width();
	if (_groups->x() < fadeRight + single - iconLeft) {
		scrollGroupsTo(fadeRight + single - iconLeft);
	} else if (_groups->x() > width() - single - iconRight) {
		scrollGroupsTo(width() - single - iconRight);
	} else {
		_groupsLeftAnimation.stop();
	}
}

void SearchWithGroups::scrollGroupsToStart() {
	scrollGroupsTo(width());
}

void SearchWithGroups::scrollGroupsTo(int left) {
	left = clampGroupsLeft(width(), left);
	_groupsLeftTo = left;
	const auto delta = _groupsLeftTo - _groups->x();
	if (!delta) {
		_groupsLeftAnimation.stop();
		return;
	}
	_groupsLeftAnimation.start([=] {
		const auto d = int(base::SafeRound(_groupsLeftAnimation.value(0)));
		moveGroupsTo(width(), _groupsLeftTo - d);
	}, delta, 0, st::slideWrapDuration, anim::sineInOut);
}

void SearchWithGroups::initEdges() {
	paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(this).fillRect(clip, _st.bg);
	}, lifetime());

	const auto makeEdge = [&](bool left) {
		const auto edge = CreateChild<RpWidget>(this);
		const auto size = QSize(height() / 2, height());
		edge->setAttribute(Qt::WA_TransparentForMouseEvents);
		edge->resize(size);
		if (left) {
			edge->move(0, 0);
		} else {
			widthValue(
			) | rpl::start_with_next([=](int width) {
				edge->move(width - edge->width(), 0);
			}, edge->lifetime());
		}
		edge->paintRequest(
		) | rpl::start_with_next([=] {
			const auto ratio = edge->devicePixelRatioF();
			ensureRounding(height(), ratio);
			const auto size = _rounding.height();
			const auto half = size / 2;
			QPainter(edge).drawImage(
				QPoint(),
				_rounding,
				QRect(left ? 0 : _rounding.width() - half, 0, half, size));
		}, edge->lifetime());
	};
	makeEdge(true);
	makeEdge(false);

	_fadeOpacity.changes(
	) | rpl::start_with_next([=] {
		_fade->update();
	}, _fade->lifetime());

	_fade->paintRequest(
	) | rpl::start_with_next([=](QRect clip) {
		auto p = QPainter(_fade);
		p.setOpacity(_fadeOpacity.current());
		const auto fill = QRect(0, 0, _fadeLeftStart, _st.height);
		if (fill.intersects(clip)) {
			p.fillRect(fill, _st.bg);
		}
		const auto icon = QRect(
			_fadeLeftStart,
			0,
			_st.fadeLeft.width(),
			_st.height);
		if (clip.intersects(icon)) {
			_st.fadeLeft.fill(p, icon);
		}
	}, _fade->lifetime());
	_fade->setAttribute(Qt::WA_TransparentForMouseEvents);

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_rounding = QImage();
	}, lifetime());
}

void SearchWithGroups::initButtons() {
	_cancel->setClickedCallback([=] {
		_field->setText(QString());
	});
	_back->entity()->setClickedCallback([=] {
		_chosenGroup = QString();
		scrollGroupsToStart();
	});
	_search->entity()->setClickedCallback([=] {
		_field->setFocus();
		scrollGroupsToStart();
	});
	_field->focusedChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::start_with_next([=] {
		scrollGroupsToStart();
	}, _field->lifetime());
	_field->raise();
	_fade->raise();
	_search->raise();
	_back->raise();
	_cancel->raise();
}

void SearchWithGroups::ensureRounding(int size, float64 ratio) {
	const auto rounded = qRound(size * ratio);
	const auto full = QSize(rounded + 4, rounded);
	if (_rounding.size() != full) {
		_rounding = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_rounding.fill(_st.outer->c);
		auto p = QPainter(&_rounding);
		auto hq = PainterHighQualityEnabler(p);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(Qt::transparent);
		p.setPen(Qt::NoPen);
		p.drawRoundedRect(QRect(QPoint(), full), rounded / 2., rounded / 2.);
	}
	_rounding.setDevicePixelRatio(ratio);
}

rpl::producer<> SearchWithGroups::escapes() const {
	return _field->cancelled();
}

rpl::producer<std::vector<QString>> SearchWithGroups::queryValue() const {
	return _query.value();
}

auto SearchWithGroups::debouncedQueryValue() const
-> rpl::producer<std::vector<QString>> {
	return _debouncedQuery.value();
}

void SearchWithGroups::cancel() {
	_field->setText(QString());
	_chosenGroup = QString();
	scrollGroupsToStart();
}

void SearchWithGroups::setLoading(bool loading) {
	_cancel->setLoadingAnimation(loading);
}

void SearchWithGroups::stealFocus() {
	if (!_focusTakenFrom) {
		_focusTakenFrom = QApplication::focusWidget();
	}
	_field->setFocus();
}

void SearchWithGroups::returnFocus() {
	if (_field && _focusTakenFrom) {
		if (_field->hasFocus()) {
			_focusTakenFrom->setFocus();
		}
		_focusTakenFrom = nullptr;
	}
}

int SearchWithGroups::IconSizeOverride() {
	return style::ConvertScale(kCategoryIconSizeOverride);
}

int SearchWithGroups::resizeGetHeight(int newWidth) {
	if (!newWidth) {
		return _st.height;
	}
	_back->moveToLeft(0, 0, newWidth);
	_search->moveToLeft(0, 0, newWidth);
	_cancel->moveToRight(0, 0, newWidth);

	moveGroupsBy(newWidth, 0);

	const auto fadeWidth = _fadeLeftStart + _st.fadeLeft.width();
	const auto fade = QRect(0, 0, fadeWidth, _st.height);
	_fade->setGeometry(fade);

	return _st.height;
}

void SearchWithGroups::wheelEvent(QWheelEvent *e) {
	static_cast<GroupsStrip*>(_groups->entity())->scrollByWheel(e);
}

int SearchWithGroups::clampGroupsLeft(int width, int desiredLeft) const {
	const auto groupsLeftDefault = _fadeLeftStart + _st.defaultFieldWidth;
	const auto groupsLeftMin = width - _groups->entity()->width();
	const auto groupsLeftMax = std::max(groupsLeftDefault, groupsLeftMin);
	return std::clamp(desiredLeft, groupsLeftMin, groupsLeftMax);
}

void SearchWithGroups::moveGroupsBy(int width, int delta) {
	moveGroupsTo(width, _groups->x() + delta);
}

void SearchWithGroups::moveGroupsTo(int width, int to) {
	const auto groupsLeft = clampGroupsLeft(width, to);
	_groups->move(groupsLeft, 0);

	const auto placeholderMargins = _st.field.textMargins
		+ _st.field.placeholderMargins;
	const auto placeholderWidth = _fieldPlaceholderWidth.current();
	const auto fieldWidthMin = std::min(
		rect::m::sum::h(placeholderMargins) + placeholderWidth,
		_st.defaultFieldWidth);
	const auto fieldWidth = _fieldEmpty.current()
		? std::max(groupsLeft - _st.search.width, fieldWidthMin)
		: (width - _fadeLeftStart - _st.cancel.width);
	_field->resizeToWidth(fieldWidth);
	const auto fieldLeft = _fieldEmpty.current()
		? (groupsLeft - fieldWidth)
		: _fadeLeftStart;
	_field->moveToLeft(fieldLeft, 0);

	if (fieldLeft >= _fadeLeftStart) {
		if (!_fade->isHidden()) {
			_fade->hide();
		}
	} else {
		if (_fade->isHidden()) {
			_fade->show();
		}
		_fadeOpacity = (fieldLeft < _fadeLeftStart / 2)
			? 1.
			: (_fadeLeftStart - fieldLeft) / float64(_fadeLeftStart / 2);
	}
}

TabbedSearch::TabbedSearch(
	not_null<RpWidget*> parent,
	const style::EmojiPan &st,
	SearchDescriptor &&descriptor)
: _st(st)
, _search(parent, std::move(descriptor)) {
	_search.move(_st.searchMargin.left(), _st.searchMargin.top());

	parent->widthValue(
	) | rpl::start_with_next([=](int width) {
		_search.resizeToWidth(width - rect::m::sum::h(_st.searchMargin));
	}, _search.lifetime());
}

int TabbedSearch::height() const {
	return _search.height() + rect::m::sum::v(_st.searchMargin);
}

QImage TabbedSearch::grab() {
	return Ui::GrabWidgetToImage(&_search);
}

void TabbedSearch::cancel() {
	_search.cancel();
}

void TabbedSearch::setLoading(bool loading) {
	_search.setLoading(loading);
}

void TabbedSearch::stealFocus() {
	_search.stealFocus();
}

void TabbedSearch::returnFocus() {
	_search.returnFocus();
}

rpl::producer<> TabbedSearch::escapes() const {
	return _search.escapes();
}

rpl::producer<std::vector<QString>> TabbedSearch::queryValue() const {
	return _search.queryValue();
}

auto TabbedSearch::debouncedQueryValue() const
-> rpl::producer<std::vector<QString>> {
	return _search.debouncedQueryValue();
}

} // namespace Ui

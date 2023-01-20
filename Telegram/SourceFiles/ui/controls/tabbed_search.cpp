/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/tabbed_search.h"

#include "lang/lang_keys.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace {

constexpr auto kDebounceTimeout = crl::time(400);

class GroupsStrip final : public RpWidget {
public:
	GroupsStrip(
		QWidget *parent,
		const style::TabbedSearch &st,
		rpl::producer<std::vector<EmojiGroup>> groups,
		Text::CustomEmojiFactory factory);

	[[nodiscard]] rpl::producer<EmojiGroup> chosen() const;
	void clearChosen();

private:
	struct Button {
		EmojiGroup group;
		QString iconId;
		std::unique_ptr<Ui::Text::CustomEmoji> icon;
	};

	void init(rpl::producer<std::vector<EmojiGroup>> groups);
	void set(std::vector<EmojiGroup> list);

	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	static inline auto FindById(auto &&buttons, QStringView id) {
		return ranges::find(buttons, id, &Button::iconId);
	}

	const style::TabbedSearch &_st;
	const Text::CustomEmojiFactory _factory;

	std::vector<Button> _buttons;
	rpl::event_stream<EmojiGroup> _chosenGroup;
	int _selected = -1;
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

rpl::producer<EmojiGroup> GroupsStrip::chosen() const {
	return _chosenGroup.events();
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
			_chosenGroup.fire_copy(i->group);
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
	const auto height = this->height();
	const auto clip = e->rect();
	const auto now = crl::now();
	for (const auto &button : _buttons) {
		const auto left = index * single;
		const auto top = 0;
		const auto size = Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
		if (_chosen == index) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgRipple);
			p.drawEllipse(left, top + (height - single) / 2, single, single);
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

void GroupsStrip::mousePressEvent(QMouseEvent *e) {
	const auto index = e->pos().x() / _st.groupWidth;
	const auto chosen = (index < 0 || index >= _buttons.size())
		? -1
		: index;
	_pressed = chosen;
}

void GroupsStrip::mouseReleaseEvent(QMouseEvent *e) {
	const auto index = e->pos().x() / _st.groupWidth;
	const auto chosen = (index < 0 || index >= _buttons.size())
		? -1
		: index;
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed == index && index >= 0) {
		_chosen = pressed;
		_chosenGroup.fire_copy(_buttons[index].group);
		update();
	}
}

} // namespace

SearchWithGroups::SearchWithGroups(
	QWidget *parent,
	SearchDescriptor descriptor)
: RpWidget(parent)
, _st(descriptor.st)
, _search(CreateChild<FadeWrap<IconButton>>(
	this,
	object_ptr<IconButton>(this, _st.search)))
, _back(CreateChild<FadeWrap<IconButton>>(
	this,
	object_ptr<IconButton>(this, _st.back)))
, _cancel(CreateChild<CrossButton>(this, _st.cancel))
, _field(CreateChild<InputField>(this, _st.field, tr::lng_dlg_filter()))
, _groups(CreateChild<FadeWrap<RpWidget>>(
	this,
	object_ptr<GroupsStrip>(
		this,
		_st,
		std::move(descriptor.groups),
		std::move(descriptor.customEmojiFactory))))
, _fadeLeft(CreateChild<FadeWrap<RpWidget>>(
	this,
	object_ptr<RpWidget>(this)))
, _fadeRight(CreateChild<FadeWrap<RpWidget>>(
	this,
	object_ptr<RpWidget>(this)))
, _debounceTimer([=] { _debouncedQuery = _query.current(); }) {
	initField();
	initGroups();
	initEdges();
}

anim::type SearchWithGroups::animated() const {
	return _inited ? anim::type::normal : anim::type::instant;
}

void SearchWithGroups::initField() {
	connect(_field, &InputField::changed, [=] {
		const auto last = FieldQuery(_field);
		_query = last;
		const auto empty = last.empty();
		_cancel->toggle(!empty, animated());
		_groups->toggle(empty, animated());
		if (empty) {
			_debounceTimer.cancel();
			_debouncedQuery = last;
		} else {
			_debounceTimer.callOnce(kDebounceTimeout);
			_chosenGroup = QString();
		}
	});

	_fieldPlaceholderWidth = tr::lng_dlg_filter(
	) | rpl::map([=](const QString &value) {
		return _st.field.placeholderFont->width(value);
	}) | rpl::after_next([=] {
		resizeToWidth(width());
	});

	const auto last = FieldQuery(_field);
	_query = last;
	_debouncedQuery = last;
}

void SearchWithGroups::initGroups() {
	const auto widget = static_cast<GroupsStrip*>(_groups->entity());

	_groups->move(_search->entity()->width() + _st.defaultFieldWidth, 0);
	widget->resize(widget->width(), _st.height);
	widget->widthValue(
	) | rpl::filter([=] {
		return (width() > 0);
	}) | rpl::start_with_next([=] {
		resizeToWidth(width());
	}, widget->lifetime());

	widget->chosen(
	) | rpl::start_with_next([=](const EmojiGroup &group) {
		_chosenGroup = group.iconId;
		_query = group.emoticons;
		_debouncedQuery = group.emoticons;
		_debounceTimer.cancel();
	}, lifetime());

	_chosenGroup.value(
	) | rpl::map([=](const QString &id) {
		return id.isEmpty();
	}) | rpl::start_with_next([=](bool empty) {
		_search->toggle(empty, animated());
		_back->toggle(!empty, animated());
		if (empty) {
			widget->clearChosen();
		} else {
			_field->setText({});
		}
	}, lifetime());
}

void SearchWithGroups::initEdges() {
	paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(this).fillRect(clip, _st.bg);
	}, lifetime());

	const auto makeEdge = [&](bool left) {
		const auto edge = CreateChild<RpWidget>(this);
		const auto size = QSize(height() / 2, height());
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

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		_rounding = QImage();
	}, lifetime());
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

rpl::producer<std::vector<QString>> SearchWithGroups::queryValue() const {
	return _query.value();
}

auto SearchWithGroups::debouncedQueryValue() const
-> rpl::producer<std::vector<QString>> {
	return _debouncedQuery.value();
}

int SearchWithGroups::resizeGetHeight(int newWidth) {
	_back->moveToLeft(0, 0, newWidth);
	_search->moveToLeft(0, 0, newWidth);
	_cancel->moveToRight(0, 0, newWidth);

	const auto searchWidth = _search->entity()->width();
	const auto groupsLeftDefault = searchWidth + _st.defaultFieldWidth;
	const auto groupsLeftMin = newWidth - _groups->entity()->width();
	const auto groupsLeftMax = std::max(groupsLeftDefault, groupsLeftMin);
	const auto groupsLeft = std::clamp(
		_groups->x(),
		groupsLeftMin,
		groupsLeftMax);
	_groups->move(groupsLeft, 0);

	const auto placeholderMargins = _st.field.textMargins
		+ _st.field.placeholderMargins;
	const auto placeholderWidth = _fieldPlaceholderWidth.current();
	const auto fieldWidthMin = std::min(
		rect::m::sum::h(placeholderMargins) + placeholderWidth,
		_st.defaultFieldWidth);
	const auto fieldWidth = std::max(
		groupsLeft - searchWidth,
		fieldWidthMin);
	_field->resizeToWidth(fieldWidth);
	_field->moveToLeft(groupsLeft - fieldWidth, 0);

	return _st.height;
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

rpl::producer<std::vector<QString>> TabbedSearch::queryValue() const {
	return _search.queryValue();
}

auto TabbedSearch::debouncedQueryValue() const
-> rpl::producer<std::vector<QString>> {
	return _search.debouncedQueryValue();
}

} // namespace Ui

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_sets_manager.h"

#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/effects/radial_animation.h"
#include "ui/emoji_config.h"
#include "lang/lang_keys.h"
#include "layout.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace Ui {
namespace Emoji {
namespace {

struct Available {
	int size = 0;

	inline bool operator<(const Available &other) const {
		return size < other.size;
	}
	inline bool operator==(const Available &other) const {
		return size == other.size;
	}
};
struct Ready {
	inline bool operator<(const Ready &other) const {
		return false;
	}
	inline bool operator==(const Ready &other) const {
		return true;
	}
};
struct Active {
	inline bool operator<(const Active &other) const {
		return false;
	}
	inline bool operator==(const Active &other) const {
		return true;
	}
};
struct Loading {
	int offset = 0;
	int size = 0;

	inline bool operator<(const Loading &other) const {
		return (offset < other.offset)
			|| (offset == other.offset && size < other.size);
	}
	inline bool operator==(const Loading &other) const {
		return (offset == other.offset) && (size == other.size);
	}
};
struct Failed {
	inline bool operator<(const Failed &other) const {
		return false;
	}
	inline bool operator==(const Failed &other) const {
		return true;
	}
};
using SetState = base::variant<
	Available,
	Ready,
	Active,
	Loading,
	Failed>;

class Loader : public QObject {
public:
	explicit Loader(int id);

	int id() const;

	rpl::producer<SetState> state() const;

private:
	int _id = 0;
	int _size = 0;
	rpl::variable<SetState> _state;

};

class Inner : public Ui::RpWidget {
public:
	Inner(QWidget *parent);

private:
	void setupContent();

};

class Row : public Ui::RippleButton {
public:
	Row(QWidget *widget, const Set &set);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setupContent(const Set &set);
	void setupCheck();
	void setupLabels(const Set &set);
	void setupHandler();

	int _id = 0;
	rpl::variable<SetState> _state;
	std::unique_ptr<Ui::RadialAnimation> _loading;

};

QPointer<Loader> GlobalLoader;
rpl::event_stream<Loader*> GlobalLoaderValues;

int GetDownloadSize(int id) {
	const auto sets = Sets();
	return ranges::find(sets, id, &Set::id)->size;
}

SetState ComputeState(int id) {
	if (id == CurrentSetId()) {
		return Active();
	} else if (SetIsReady(id)) {
		return Ready();
	}
	return Available{ GetDownloadSize(id) };
}

QString StateDescription(const SetState &state) {
	return state.match([](const Available &data) {
		return lng_emoji_set_available(lt_size, formatSizeText(data.size));
	}, [](const Ready &data) -> QString {
		return lang(lng_emoji_set_ready);
	}, [](const Active &data) -> QString {
		return lang(lng_emoji_set_active);
	}, [](const Loading &data) {
		return lng_emoji_set_loading(
			lt_progress,
			formatDownloadText(data.offset, data.size));
	}, [](const Failed &data) {
		return lang(lng_attach_failed);
	});
}

Loader::Loader(int id)
: _id(id)
, _size(GetDownloadSize(_id))
, _state(Loading{ 0, _size }) {
}

int Loader::id() const {
	return _id;
}

rpl::producer<SetState> Loader::state() const {
	return _state.value();
}

Inner::Inner(QWidget *parent) : RpWidget(parent) {
	setupContent();
}

void Inner::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	const auto sets = Sets();
	for (const auto &set : sets) {
		content->add(object_ptr<Row>(content, set));
	}

	content->resizeToWidth(st::boxWidth);
	Ui::ResizeFitChild(this, content);
}

Row::Row(QWidget *widget, const Set &set)
: RippleButton(widget, st::contactsRipple)
, _id(set.id)
, _state(Available{ set.size }) {
	setupContent(set);
	setupHandler();
}

void Row::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (isDisabled()) {
		return;
	}
	const auto over = isOver() || isDown();
	const auto bg = over ? st::windowBgOver : st::windowBg;
	p.fillRect(rect(), bg);

	const auto ms = getms();
	paintRipple(p, 0, 0, ms);
}

void Row::setupContent(const Set &set) {
	_state = GlobalLoaderValues.events_starting_with(
		GlobalLoader.data()
	) | rpl::map([=](Loader *loader) {
		return (loader && loader->id() == _id)
			? loader->state()
			: rpl::single(
				rpl::empty_value()
			) | rpl::then(
				Updated()
			) | rpl::map([=] {
				return ComputeState(_id);
			});
	}) | rpl::flatten_latest(
	) | rpl::filter([=](const SetState &state) {
		return !_state.current().is<Failed>() || !state.is<Available>();
	});

	setupCheck();
	setupLabels(set);

	resize(width(), st::defaultPeerList.item.height);
}

void Row::setupHandler() {
	clicks(
	) | rpl::filter([=] {
		const auto &state = _state.current();
		return state.is<Ready>() || state.is<Available>();
	}) | rpl::start_with_next([=] {
		App::CallDelayed(st::defaultRippleAnimation.hideDuration, this, [=] {
			if (!SwitchToSet(_id)) {
				// load
			} else {
				delete GlobalLoader;
			}
		});
	}, lifetime());

	_state.value(
	) | rpl::map([](const SetState &state) {
		return state.is<Ready>() || state.is<Available>();
	}) | rpl::start_with_next([=](bool active) {
		setDisabled(!active);
		setPointerCursor(active);
	}, lifetime());
}

void Row::setupCheck() {
	using namespace rpl::mappers;

	const auto check = Ui::CreateChild<Ui::FadeWrapScaled<Ui::IconButton>>(
		this,
		object_ptr<Ui::IconButton>(
			this,
			st::manageEmojiCheck));
	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto checkx = size.width()
			- (st::contactsPadding.right()
				+ st::contactsCheckPosition.x()
				+ check->width());
		const auto checky = st::contactsPadding.top()
			+ (st::contactsPhotoSize - check->height()) / 2;
		check->moveToLeft(checkx, checky);
	}, check->lifetime());

	check->toggleOn(_state.value(
	) | rpl::map(
		_1 == Active()
	) | rpl::distinct_until_changed());

	check->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void Row::setupLabels(const Set &set) {
	using namespace rpl::mappers;

	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		this,
		set.name,
		Ui::FlatLabel::InitType::Simple,
		st::localStorageRowTitle);
	const auto status = Ui::CreateChild<Ui::FlatLabel>(
		this,
		_state.value() | rpl::map(StateDescription),
		st::localStorageRowSize);

	sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto left = st::contactsPadding.left();
		const auto namey = st::contactsPadding.top()
			+ st::contactsNameTop;
		const auto statusy = st::contactsPadding.top()
			+ st::contactsStatusTop;
		name->moveToLeft(left, namey);
		status->moveToLeft(left, statusy);
	}, name->lifetime());
}

} // namespace

ManageSetsBox::ManageSetsBox(QWidget*) {
}

void ManageSetsBox::prepare() {
	const auto inner = setInnerWidget(object_ptr<Inner>(this));

	setTitle(langFactory(lng_emoji_manage_sets));

	addButton(langFactory(lng_close), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, inner);
}

} // namespace Emoji
} // namespace Ui

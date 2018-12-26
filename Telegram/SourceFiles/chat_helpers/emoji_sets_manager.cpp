/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/emoji_sets_manager.h"

#include "mtproto/dedicated_file_loader.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/effects/radial_animation.h"
#include "ui/emoji_config.h"
#include "lang/lang_keys.h"
#include "base/zlib_help.h"
#include "layout.h"
#include "messenger.h"
#include "mainwidget.h"
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
using Loading = MTP::DedicatedLoader::Progress;
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
	Loader(QObject *parent, int id);

	int id() const;

	rpl::producer<SetState> state() const;

private:
	void setImplementation(std::unique_ptr<MTP::DedicatedLoader> loader);
	void unpack(const QString &path);
	void finalize(const QString &path);
	bool goodName(const QString &name) const;
	bool writeCurrentFile(zlib::FileToRead &zip, const QString name) const;
	void destroy();
	void fail();

	int _id = 0;
	int _size = 0;
	rpl::variable<SetState> _state;

	MTP::WeakInstance _mtproto;
	std::unique_ptr<MTP::DedicatedLoader> _implementation;

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
	void load();

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

MTP::DedicatedLoader::Location GetDownloadLocation(int id) {
	constexpr auto kUsername = "tdhbcfiles";
	const auto sets = Sets();
	const auto i = ranges::find(sets, id, &Set::id);
	return MTP::DedicatedLoader::Location{ kUsername, i->postId };
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
			formatDownloadText(data.already, data.size));
	}, [](const Failed &data) {
		return lang(lng_attach_failed);
	});
}

QByteArray ReadFinalFile(const QString &path) {
	constexpr auto kMaxZipSize = 10 * 1024 * 1024;
	auto file = QFile(path);
	if (file.size() > kMaxZipSize || !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

Loader::Loader(QObject *parent, int id)
: QObject(parent)
, _id(id)
, _size(GetDownloadSize(_id))
, _state(Loading{ 0, _size })
, _mtproto(Messenger::Instance().mtp()) {
	const auto ready = [=](std::unique_ptr<MTP::DedicatedLoader> loader) {
		if (loader) {
			setImplementation(std::move(loader));
		} else {
			fail();
		}
	};
	const auto location = GetDownloadLocation(id);
	const auto folder = internal::SetDataPath(id);
	MTP::StartDedicatedLoader(&_mtproto, location, folder, ready);
}

int Loader::id() const {
	return _id;
}

rpl::producer<SetState> Loader::state() const {
	return _state.value();
}

void Loader::setImplementation(
		std::unique_ptr<MTP::DedicatedLoader> loader) {
	_implementation = std::move(loader);
	auto convert = [](auto value) {
		return SetState(value);
	};
	_state = _implementation->progress(
	) | rpl::map([](const Loading &state) {
		return SetState(state);
	});
	_implementation->failed(
	) | rpl::start_with_next([=] {
		fail();
	}, _implementation->lifetime());

	_implementation->ready(
	) | rpl::start_with_next([=](const QString &filepath) {
		unpack(filepath);
	}, _implementation->lifetime());

	QDir(internal::SetDataPath(_id)).removeRecursively();
	_implementation->start();
}

void Loader::unpack(const QString &path) {
	const auto bytes = ReadFinalFile(path);
	if (bytes.isEmpty()) {
		return fail();
	}

	auto zip = zlib::FileToRead(bytes);
	if (zip.goToFirstFile() != UNZ_OK) {
		return fail();
	}
	do {
		const auto name = zip.getCurrentFileName();
		if (goodName(name) && !writeCurrentFile(zip, name)) {
			return fail();
		}

		const auto jump = zip.goToNextFile();
		if (jump == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (jump != UNZ_OK) {
			return fail();
		}
	} while (true);

	finalize(path);
}

bool Loader::goodName(const QString &name) const {
	return (name == qstr("config.json"))
		|| (name.startsWith(qstr("emoji_"))
			&& name.endsWith(qstr(".webp")));
}

void Loader::finalize(const QString &path) {
	QFile(path).remove();
	if (!SwitchToSet(_id)) {
		fail();
	} else {
		destroy();
	}
}

void Loader::fail() {
	_state = Failed();
}

void Loader::destroy() {
	GlobalLoaderValues.fire(nullptr);
	delete this;
}

bool Loader::writeCurrentFile(
		zlib::FileToRead &zip,
		const QString name) const {
	constexpr auto kMaxSize = 10 * 1024 * 1024;
	const auto content = zip.readCurrentFileContent(kMaxSize);
	if (content.isEmpty() || zip.error() != UNZ_OK) {
		return false;
	}
	const auto folder = internal::SetDataPath(_id) + '/';
	auto file = QFile(folder + name);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size());
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
		if (_state.current().is<Available>()) {
			load();
			return;
		}
		const auto delay = st::defaultRippleAnimation.hideDuration;
		App::CallDelayed(delay, this, [=] {
			if (!SwitchToSet(_id)) {
				load();
			} else {
				delete GlobalLoader;
			}
		});
	}, lifetime());

	_state.value(
	) | rpl::map([=](const SetState &state) {
		return state.is<Ready>() || state.is<Available>();
	}) | rpl::start_with_next([=](bool active) {
		setDisabled(!active);
		setPointerCursor(active);
	}, lifetime());
}

void Row::load() {
	GlobalLoader = Ui::CreateChild<Loader>(App::main(), _id);
	GlobalLoaderValues.fire(GlobalLoader.data());
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
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto status = Ui::CreateChild<Ui::FlatLabel>(
		this,
		_state.value() | rpl::map(StateDescription),
		st::localStorageRowSize);
	status->setAttribute(Qt::WA_TransparentForMouseEvents);

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

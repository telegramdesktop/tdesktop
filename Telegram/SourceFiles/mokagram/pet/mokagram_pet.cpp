// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 6c1f0a7e
// Mokagram: питомец Clawd на верхнем крае баббла поля ввода.
#include "mokagram/pet/mokagram_pet.h"

#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/settings/mokagram_settings.h"
#include "apiwrap.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/effects/animations.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

#include <QtCore/QTimer>
#include <QtGui/QCursor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>

#include <cmath>
#include <cstdio>

namespace Mokagram::Pet {
namespace {

constexpr auto kEnabledKey = "pet.clawd";
constexpr auto kPackName = "Clawd_by_PackCreatorBot";

// Состояния питомца. Каждому соответствует эмодзи набора (см. kFrames).
enum class Role {
	Stand,
	Walk,
	Run,
	Yawn,
	Sleep,
	Look,
	Happy,
	Love,
	Held,
	Fall,
	Shake,
	Count,
};

// Кадры набора Clawd_by_PackCreatorBot для каждого состояния.
// Состав набора печатает команда отладки clawd (строки CLAWD в stderr).
struct Frame {
	int index = -1;    // номер эмодзи в наборе
	float64 foot = 0.; // где лапы, доля высоты кадра сверху
	int facing = 0;    // 1 смотрит вправо, -1 влево, 0 анфас
};
constexpr Frame kFrames[int(Role::Count)] = {
	{ 16, 0.78, 0 }, // Stand: стоит и моргает
	{ 76, 0.81, 0 }, // Walk: переваливается с боку на бок
	{ 72, 0.81, 0 }, // Run: бодро крутится на бегу
	{ 86, 0.84, 0 }, // Yawn: прикрывает глаза и ложится
	{ 40, 0.95, 0 }, // Sleep: спит, zzz
	{ 77, 0.81, 0 }, // Look: оглядывается, пока печатаешь
	{ 61, 0.99, 0 }, // Happy: ^^ после отправки
	{ 24, 0.83, 0 }, // Love: сердечки по клику
	{ 41, 0.83, 0 }, // Held: «!» когда схватили
	{ 87, 0.80, 0 }, // Fall: спирали в глазах в полёте
	{ 71, 0.81, 0 }, // Shake: трясётся после падения
};

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] float64 Rand01() {
	return base::RandomValue<uint32>() / float64(0xFFFFFFFFU);
}

class Pack final {
public:
	static Pack &Instance() {
		static auto result = Pack();
		return result;
	}

	void ensure(not_null<Main::Session*> session) {
		if (_session == session && (_requestId || !_docs.empty())) {
			return;
		}
		if (_session != session) {
			_docs.clear();
			_session = session;
		}
		_requestId = session->api().request(MTPmessages_GetStickerSet(
			MTP_inputStickerSetShortName(MTP_string(QString(kPackName))),
			MTP_int(0)
		)).done([=](const MTPmessages_StickerSet &set) {
			_requestId = 0;
			set.match([&](const MTPDmessages_stickerSet &data) {
				_docs.clear();
				for (const auto &doc : data.vdocuments().v) {
					_docs.push_back(_session->data().processDocument(doc));
				}
				_updated.fire({});
			}, [](const auto &) {});
		}).fail([=] {
			_requestId = 0;
		}).send();
	}

	[[nodiscard]] const std::vector<not_null<DocumentData*>> &docs() const {
		return _docs;
	}
	[[nodiscard]] Main::Session *session() const {
		return _session;
	}
	[[nodiscard]] rpl::producer<> updated() const {
		return _updated.events();
	}
	[[nodiscard]] DocumentData *document(Role role) const {
		const auto index = kFrames[int(role)].index;
		return (index >= 0 && index < int(_docs.size()))
			? _docs[index].get()
			: nullptr;
	}

private:
	Main::Session *_session = nullptr;
	mtpRequestId _requestId = 0;
	std::vector<not_null<DocumentData*>> _docs;
	rpl::event_stream<> _updated;

};

class Clawd final {
public:
	explicit Clawd(Host host);

	void act(const QString &action);

private:
	enum class Mode {
		Ground,
		Air,
		Held,
	};

	void tick(crl::time now);
	void think(crl::time now);
	void setRole(Role role, crl::time duration = 0);
	void wake();
	void jump(float64 speed);
	void land();
	void updateHit();
	void paint(QPainter &p, QRect clip);
	void watchdog();
	void handleHit(not_null<QEvent*> e);
	void refreshEmoji();
	void restack();

	[[nodiscard]] int size() const;
	[[nodiscard]] QRectF petRect() const;
	[[nodiscard]] QRectF bodyRect() const;
	[[nodiscard]] Role shownRole();
	[[nodiscard]] float64 groundY() const;
	[[nodiscard]] float64 ceilingY() const;
	[[nodiscard]] float64 minX() const;
	[[nodiscard]] float64 maxX() const;
	[[nodiscard]] Ui::Text::CustomEmoji *emoji(Role role);

	Host _host;
	// Виджеты удаляются вместе с питомцем: их обработчики держат this.
	const base::unique_qptr<Ui::RpWidget> _stage;
	const base::unique_qptr<Ui::RpWidget> _hit;
	Ui::Animations::Basic _animation;
	base::Timer _watchdog;

	std::unique_ptr<Ui::Text::CustomEmoji> _emoji[int(Role::Count)];
	QRect _bubble;
	Mode _mode = Mode::Ground;
	Role _role = Role::Stand;
	crl::time _roleUntil = 0;
	crl::time _nextThink = 0;
	crl::time _lastActivity = 0;
	crl::time _lastTyping = 0;
	crl::time _lastHop = 0;
	crl::time _chaseUntil = 0;
	crl::time _last = 0;
	float64 _x = -1.;
	float64 _y = 0.;
	float64 _vx = 0.;
	float64 _vy = 0.;
	float64 _targetX = -1.;
	bool _faceRight = true;
	bool _sleeping = false;
	bool _thrown = false;
	bool _pressed = false;
	QPoint _pressGlobal;
	QPointF _grabOffset;
	QPointF _dragPos;
	QPointF _dragVelocity;
	crl::time _dragTime = 0;
	QRect _painted;
	rpl::lifetime _lifetime;

};

Clawd::Clawd(Host host)
: _host(std::move(host))
, _stage(Ui::CreateChild<Ui::RpWidget>(_host.parent.get()))
, _hit(Ui::CreateChild<Ui::RpWidget>(_host.parent.get()))
, _animation([=](crl::time now) { tick(now); })
, _watchdog([=] { watchdog(); }) {
	_stage->setAttribute(Qt::WA_TransparentForMouseEvents);
	_stage->setGeometry(_host.parent->rect());
	_stage->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_stage.get());
		paint(p, clip);
	}, _stage->lifetime());
	_host.parent->sizeValue() | rpl::on_next([=](QSize size) {
		_stage->setGeometry(QRect(QPoint(), size));
	}, _stage->lifetime());

	_hit->setCursor(Qt::OpenHandCursor);
	_hit->setMouseTracking(true);
	_hit->events() | rpl::on_next([=](not_null<QEvent*> e) {
		handleHit(e);
	}, _hit->lifetime());

	restack();

	const auto now = crl::now();
	_lastActivity = now;
	_nextThink = now + 1500;

	Pack::Instance().ensure(_host.session);
	Pack::Instance().updated() | rpl::on_next([=] {
		refreshEmoji();
	}, _lifetime);
	refreshEmoji();

	std::move(_host.typing) | rpl::on_next([=] {
		const auto now = crl::now();
		_lastTyping = now;
		wake();
		if (_mode == Mode::Ground) {
			setRole(Role::Look, 2500);
			if (now - _lastHop > 650) {
				_lastHop = now;
				jump(Px(110));
			}
		}
	}, _lifetime);

	_host.session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (!item->out()
			|| !item->isSending()
			|| item->history() != _host.history()) {
			return;
		}
		wake();
		if (_mode != Mode::Held) {
			setRole(Role::Happy, 1800);
			jump(Px(260));
		}
	}, _lifetime);

	_watchdog.callEach(400);
	QObject::connect(qApp, &QGuiApplication::applicationStateChanged, _stage.get(), [=] {
		watchdog();
	});
	watchdog();
}

int Clawd::size() const {
	return Px(56);
}

float64 Clawd::groundY() const {
	// Лапы стоят на верхнем крае баббла.
	return _bubble.top() + Px(1);
}

float64 Clawd::ceilingY() const {
	// Не выше видимой ленты: под шапку и закреп не залезает.
	const auto area = _host.area ? _host.area() : QRect();
	return (area.isEmpty() ? 0 : area.top()) + size() * 0.6;
}

float64 Clawd::minX() const {
	return _bubble.left() + Px(14) + size() / 2.;
}

float64 Clawd::maxX() const {
	return _bubble.right() - Px(14) - size() / 2.;
}

QRectF Clawd::petRect() const {
	// Кадр целиком: лапы кадра (foot) на высоте _y.
	const auto s = size();
	const auto foot = kFrames[int(
		(_emoji[int(_role)] || Pack::Instance().document(_role))
			? _role
			: Role::Stand)].foot;
	return QRectF(_x - s / 2., _y - foot * s, s, s);
}

QRectF Clawd::bodyRect() const {
	// Тело без пустых полей кадра: ловит мышь и не заходит на баббл.
	const auto s = size();
	return QRectF(_x - s * 0.4, _y - s * 0.55, s * 0.8, s * 0.55);
}

Ui::Text::CustomEmoji *Clawd::emoji(Role role) {
	const auto index = int(role);
	if (!_emoji[index]) {
		const auto document = Pack::Instance().document(role);
		if (!document || Pack::Instance().session() != _host.session) {
			return nullptr;
		}
		const auto weak = base::make_weak(_stage.get());
		_emoji[index] = _host.session->data().customEmojiManager().create(
			document,
			[=] {
				if (const auto stage = weak.get()) {
					if (stage->isVisible()) {
						stage->update(_painted);
					}
				}
			},
			Data::CustomEmojiSizeTag::Isolated,
			size());
	}
	return _emoji[index].get();
}

void Clawd::restack() {
	// Над лентой и бабблом, но под кнопками поля и всплывающими панелями.
	if (const auto under = _host.under ? _host.under() : nullptr) {
		_stage->stackUnder(under);
		_hit->stackUnder(under);
	} else {
		_stage->raise();
		_hit->raise();
	}
}

void Clawd::refreshEmoji() {
	for (auto &item : _emoji) {
		item = nullptr;
	}
	_stage->update();
}

void Clawd::watchdog() {
	const auto bubble = _host.bubble ? _host.bubble() : QRect();
	const auto window = _host.parent->window();
	const auto shown = !bubble.isEmpty()
		&& _host.parent->isVisible()
		&& !window->isMinimized()
		&& !Pack::Instance().docs().empty();
	if (!shown) {
		if (_animation.animating()) {
			_animation.stop();
		}
		_stage->hide();
		_hit->hide();
		return;
	}
	if (_stage->isHidden()) {
		_stage->show();
		_hit->show();
		restack();
	}
	if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
		if (_animation.animating()) {
			_animation.stop();
		}
		return;
	}
	if (!_animation.animating()) {
		_last = 0;
		_animation.start();
	}
}

void Clawd::wake() {
	_lastActivity = crl::now();
	if (_sleeping) {
		_sleeping = false;
		setRole(Role::Shake, 900);
	}
}

void Clawd::setRole(Role role, crl::time duration) {
	_role = role;
	_roleUntil = duration ? (crl::now() + duration) : 0;
	_stage->update(_painted);
}

void Clawd::jump(float64 speed) {
	if (_mode == Mode::Held) {
		return;
	}
	if (_mode == Mode::Ground) {
		_mode = Mode::Air;
		_vx = 0.;
	}
	_vy = -speed;
}

void Clawd::land() {
	_mode = Mode::Ground;
	_y = groundY();
	_vy = 0.;
	_vx = 0.;
	if (_thrown) {
		_thrown = false;
		setRole(Role::Shake, 1100);
	} else if (_role == Role::Fall
		|| _role == Role::Held
		|| (_role == Role::Love && !_roleUntil)) {
		setRole(Role::Stand);
	}
}

void Clawd::think(crl::time now) {
	if (_mode != Mode::Ground || now < _nextThink) {
		return;
	}
	if (_role != Role::Stand && _role != Role::Walk) {
		return;
	}
	const auto idle = now - _lastActivity;
	if (!_sleeping && idle > 45000) {
		_targetX = -1.;
		setRole(Role::Yawn, 2600);
		_sleeping = true;
		return;
	}
	const auto roll = Rand01();
	const auto cursor = _host.parent->mapFromGlobal(QCursor::pos());
	const auto cursorInside = _host.parent->rect().contains(cursor);
	if (cursorInside && roll < 0.08) {
		_chaseUntil = now + 4000;
		setRole(Role::Run);
	} else if (roll < 0.55) {
		_targetX = minX() + Rand01() * std::max(0., maxX() - minX());
		setRole(Role::Walk);
	} else if (roll < 0.65 && idle > 8000) {
		_targetX = -1.;
		setRole(Role::Yawn, 2600);
	} else {
		_targetX = -1.;
		setRole(Role::Stand);
	}
	_nextThink = now + 4000 + crl::time(Rand01() * 6000);
}

void Clawd::tick(crl::time now) {
	const auto dt = _last ? std::min(now - _last, crl::time(50)) / 1000. : 0.;
	_last = now;
	_bubble = _host.bubble ? _host.bubble() : QRect();
	if (_bubble.isEmpty()) {
		return;
	}
	if (_x < 0.) {
		_x = minX() + (maxX() - minX()) * 0.75;
		_y = groundY();
	}
	if (_roleUntil && now >= _roleUntil) {
		_roleUntil = 0;
		if (_role == Role::Yawn && _sleeping) {
			setRole(Role::Sleep);
		} else if (_mode == Mode::Ground) {
			setRole(Role::Stand);
		}
	}

	switch (_mode) {
	case Mode::Held: break;
	case Mode::Ground: {
		_y = groundY();
		think(now);
		auto target = _targetX;
		auto speed = float64(Px(20));
		if (_role == Role::Run) {
			const auto cursor = _host.parent->mapFromGlobal(QCursor::pos());
			target = std::clamp(float64(cursor.x()), minX(), maxX());
			speed = Px(75);
			if (now > _chaseUntil || std::abs(target - _x) < size() / 3.) {
				target = -1.;
				setRole(Role::Stand);
			}
		} else if (_role == Role::Look) {
			if (const auto caret = _host.caret ? _host.caret() : std::nullopt) {
				_faceRight = (caret->x() >= _x);
			}
			target = -1.;
		} else if (_role != Role::Walk) {
			target = -1.;
		}
		if (target >= 0.) {
			const auto dx = target - _x;
			const auto step = speed * dt;
			_faceRight = (dx >= 0.);
			if (std::abs(dx) <= step) {
				_x = target;
				_targetX = -1.;
				if (_role == Role::Walk) {
					setRole(Role::Stand);
				}
			} else {
				_x += (dx > 0. ? step : -step);
			}
		}
		_x = std::clamp(_x, minX(), std::max(minX(), maxX()));
	} break;
	case Mode::Air: {
		const auto gravity = float64(Px(1800));
		_vy += gravity * dt;
		// Сопротивление воздуха: бросок гаснет, а не летает от стенки к стенке.
		_vx *= std::pow(0.35, dt);
		_x += _vx * dt;
		_y += _vy * dt;
		const auto left = minX();
		const auto right = std::max(minX(), maxX());
		if (_x < left || _x > right) {
			_x = std::clamp(_x, left, right);
			_vx = -_vx * 0.4;
		}
		if (_y < ceilingY()) {
			_y = ceilingY();
			_vy = std::abs(_vy) * 0.3;
		}
		const auto ground = groundY();
		// Над бабблом лапы ловит его край, сбоку от баббла тоже он:
		// питомец не проваливается в поле и не уходит за окно.
		if (_y >= ground) {
			_y = ground;
			_x = std::clamp(_x, minX(), std::max(minX(), maxX()));
			if (_thrown && _vy > Px(420)) {
				_vy = -_vy * 0.3;
				_vx *= 0.5;
			} else {
				land();
			}
		}
	} break;
	}

	updateHit();
	const auto margin = size() / 3;
	auto dirty = petRect().toAlignedRect().marginsAdded(
		{ margin, margin, margin, margin });
	_stage->update(_painted.united(dirty));
	_painted = dirty;
}

void Clawd::updateHit() {
	// Мышь ловит только тело, и никогда ниже края баббла: поле и его
	// кнопки остаются кликабельными.
	auto body = bodyRect().toAlignedRect();
	if (_mode != Mode::Held && !_pressed) {
		body.setBottom(std::min(body.bottom(), _bubble.top() - 1));
	}
	if (_hit->geometry() != body) {
		_hit->setGeometry(body);
	}
}

Role Clawd::shownRole() {
	return emoji(_role) ? _role : Role::Stand;
}

void Clawd::paint(QPainter &p, QRect clip) {
	if (_x < 0. || !clip.intersects(_painted)) {
		return;
	}
	const auto role = shownRole();
	const auto e = emoji(role);
	if (!e) {
		return;
	}
	const auto &frame = kFrames[int(role)];
	const auto s = size();
	const auto top = _y - frame.foot * s;
	const auto pivot = bodyRect().center();
	p.save();
	p.translate(pivot);
	// Кадры набора не перерисовываем, только отражаем в сторону движения.
	if (frame.facing != 0 && (_faceRight != (frame.facing > 0))) {
		p.scale(-1., 1.);
	}
	e->paint(p, {
		.textColor = QColor(255, 255, 255),
		.now = crl::now(),
		.position = QPoint(
			int(std::round(_x - s / 2. - pivot.x())),
			int(std::round(top - pivot.y()))),
	});
	p.restore();
}

void Clawd::handleHit(not_null<QEvent*> e) {
	const auto type = e->type();
	if (type != QEvent::MouseButtonPress
		&& type != QEvent::MouseMove
		&& type != QEvent::MouseButtonRelease) {
		return;
	}
	const auto mouse = static_cast<QMouseEvent*>(e.get());
	const auto global = mouse->globalPosition().toPoint();
	const auto local = QPointF(_host.parent->mapFromGlobal(global));
	const auto now = crl::now();
	if (type == QEvent::MouseButtonPress) {
		if (mouse->button() != Qt::LeftButton) {
			return;
		}
		_pressed = true;
		_pressGlobal = global;
		_grabOffset = QPointF(_x, _y) - local;
		_dragPos = local;
		_dragVelocity = {};
		_dragTime = now;
		return;
	} else if (type == QEvent::MouseMove) {
		if (!_pressed) {
			return;
		}
		if (_mode != Mode::Held
			&& (global - _pressGlobal).manhattanLength() > Px(6)) {
			_mode = Mode::Held;
			_targetX = -1.;
			wake();
			setRole(Role::Held);
			_hit->setCursor(Qt::ClosedHandCursor);
		}
		if (_mode == Mode::Held) {
			const auto dt = std::max(now - _dragTime, crl::time(1)) / 1000.;
			const auto velocity = (local - _dragPos) / dt;
			_dragVelocity = _dragVelocity * 0.6 + velocity * 0.4;
			_dragPos = local;
			_dragTime = now;
			_x = std::clamp(local.x() + _grabOffset.x(),
				minX(),
				std::max(minX(), maxX()));
			_y = std::clamp(local.y() + _grabOffset.y(),
				ceilingY(),
				groundY());
		}
		return;
	}
	// MouseButtonRelease
	if (!_pressed || mouse->button() != Qt::LeftButton) {
		return;
	}
	_pressed = false;
	_hit->setCursor(Qt::OpenHandCursor);
	if (_mode == Mode::Held) {
		const auto limit = float64(Px(1300));
		const auto stale = (now - _dragTime > 120);
		_vx = stale ? 0. : std::clamp(_dragVelocity.x(), -limit, limit);
		_vy = stale ? 0. : std::clamp(_dragVelocity.y(), -limit, limit);
		_thrown = true;
		_mode = Mode::Air;
		setRole(Role::Fall);
	} else {
		wake();
		setRole(Role::Love, 1400);
		jump(Px(300));
	}
}

void Clawd::act(const QString &action) {
	const auto now = crl::now();
	if (action == u"click"_q) {
		wake();
		setRole(Role::Love, 1400);
		jump(Px(300));
	} else if (action == u"throw"_q) {
		wake();
		_mode = Mode::Air;
		_thrown = true;
		_y = groundY() - Px(160);
		_vx = Px(500);
		_vy = -Px(600);
		setRole(Role::Fall);
	} else if (action == u"sleep"_q) {
		_lastActivity = now - 60000;
		_nextThink = now;
		setRole(Role::Stand);
	} else if (action == u"sent"_q) {
		wake();
		setRole(Role::Happy, 1800);
		jump(Px(260));
	} else if (action == u"chase"_q) {
		wake();
		_chaseUntil = now + 4000;
		setRole(Role::Run);
	} else if (action == u"walk"_q) {
		wake();
		_targetX = (_x > (minX() + maxX()) / 2.) ? minX() : maxX();
		setRole(Role::Walk);
	} else if (action.startsWith(u"role:"_q)) {
		const auto index = action.mid(5).toInt();
		if (index >= 0 && index < int(Role::Count)) {
			setRole(Role(index), 4000);
		}
	}
}

base::flat_map<Ui::RpWidget*, Clawd*> &Alive() {
	static auto result = base::flat_map<Ui::RpWidget*, Clawd*>();
	return result;
}

// Лист всех эмодзи набора с номерами: чтобы раскладывать состояния по кадрам.
struct Sheet {
	std::vector<std::unique_ptr<Ui::Text::CustomEmoji>> items;
};

Sheet &DebugSheet() {
	// Не удаляется при выходе: эмодзи нельзя разрушать после сессии.
	static const auto result = new Sheet();
	return *result;
}

void SaveSheet(const QString &path) {
	const auto &items = DebugSheet().items;
	const auto cell = 140;
	const auto columns = 6;
	const auto rows = (int(items.size()) + columns - 1) / columns;
	auto image = QImage(
		QSize(columns * cell, std::max(rows, 1) * cell),
		QImage::Format_ARGB32_Premultiplied);
	image.fill(QColor(0x1c, 0x1c, 0x1c));
	auto p = QPainter(&image);
	p.setPen(QColor(0x8e, 0x8e, 0x93));
	const auto &docs = Pack::Instance().docs();
	for (auto i = 0; i != int(items.size()); ++i) {
		const auto x = (i % columns) * cell;
		const auto y = (i / columns) * cell;
		items[i]->paint(p, {
			.textColor = QColor(255, 255, 255),
			.now = crl::now(),
			.position = QPoint(x + 18, y + 6),
		});
		const auto alt = (i < int(docs.size()) && docs[i]->sticker())
			? docs[i]->sticker()->alt
			: QString();
		p.drawText(
			QRect(x, y + cell - 28, cell, 24),
			Qt::AlignCenter,
			QString::number(i) + u"  "_q + alt);
	}
	p.end();
	image.save(path);
}

} // namespace

void RegisterSettings() {
	namespace S = Mokagram::Settings;
	S::Register({
		.key = QString::fromUtf8(kEnabledKey),
		.section = QString::fromUtf8(Mokagram::Lang::Tr("Питомец")),
		.title = QString::fromUtf8(Mokagram::Lang::Tr("Clawd над полем ввода")),
		.description = QString::fromUtf8(Mokagram::Lang::Tr(
			"Гуляет по краю поля ввода, следит за набором и радуется отправке. Его можно взять мышкой и бросить.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
		.category = u"look"_q,
		.sectionIcon = u"bot"_q,
	});
}

void Attach(Host host) {
	struct Holder {
		Holder(Ui::RpWidget *parent, Host host)
		: parent(parent)
		, host(std::move(host)) {
		}
		~Holder() {
			Alive().remove(parent);
		}
		Ui::RpWidget *parent = nullptr;
		Host host;
		rpl::event_stream<> typing;
		std::unique_ptr<Clawd> clawd;
	};
	const auto parent = host.parent;
	const auto holder = parent->lifetime().make_state<Holder>(
		parent.get(),
		std::move(host));
	std::move(holder->host.typing) | rpl::on_next([=] {
		holder->typing.fire({});
	}, parent->lifetime());
	Mokagram::Settings::BoolValue(
		QString::fromUtf8(kEnabledKey)
	) | rpl::on_next([=](bool enabled) {
		if (!enabled) {
			Alive().remove(parent.get());
			holder->clawd = nullptr;
			return;
		} else if (holder->clawd) {
			return;
		}
		const auto &h = holder->host;
		holder->clawd = std::make_unique<Clawd>(Host{
			.parent = h.parent,
			.session = h.session,
			.bubble = h.bubble,
			.history = h.history,
			.caret = h.caret,
			.area = h.area,
			.under = h.under,
			.typing = holder->typing.events(),
		});
		Alive()[parent.get()] = holder->clawd.get();
	}, parent->lifetime());
}

bool DebugCommand(const QString &cmd, not_null<Main::Session*> session) {
	if (cmd == u"clawd"_q) {
		Pack::Instance().ensure(session);
		const auto &docs = Pack::Instance().docs();
		fprintf(stderr, "CLAWD count %d\n", int(docs.size()));
		for (auto i = 0; i != int(docs.size()); ++i) {
			const auto sticker = docs[i]->sticker();
			fprintf(
				stderr,
				"CLAWD %d %s type=%d\n",
				i,
				sticker ? sticker->alt.toUtf8().constData() : "",
				sticker ? int(sticker->type) : -1);
		}
		fflush(stderr);
		return true;
	} else if (cmd.startsWith(u"clawdsheet:"_q)) {
		Pack::Instance().ensure(session);
		auto &sheet = DebugSheet();
		const auto &docs = Pack::Instance().docs();
		if (sheet.items.size() != docs.size()) {
			sheet.items.clear();
			for (const auto &doc : docs) {
				sheet.items.push_back(session->data().customEmojiManager().create(
					doc,
					[] {},
					Data::CustomEmojiSizeTag::Isolated,
					104));
			}
		}
		SaveSheet(cmd.mid(11));
		return true;
	} else if (cmd.startsWith(u"clawdfilm:"_q)) {
		// clawdfilm:<png>:<i,j,...> 12 кадров каждой анимации за 3 секунды.
		const auto parts = cmd.mid(10).split(':');
		if (parts.size() != 2) {
			return true;
		}
		const auto path = parts[0];
		auto indices = std::vector<int>();
		for (const auto &part : parts[1].split(',')) {
			indices.push_back(part.toInt());
		}
		const auto &docs = Pack::Instance().docs();
		struct Film {
			std::vector<std::unique_ptr<Ui::Text::CustomEmoji>> items;
			std::vector<int> indices;
			QImage image;
			QImage scratch;
			int tick = 0;
		};
		const auto film = std::make_shared<Film>();
		for (const auto index : indices) {
			if (index >= 0 && index < int(docs.size())) {
				film->indices.push_back(index);
				film->items.push_back(session->data().customEmojiManager().create(
					docs[index],
					[] {},
					Data::CustomEmojiSizeTag::Isolated,
					64));
			}
		}
		const auto cell = 72;
		const auto frames = 12;
		film->image = QImage(
			QSize((frames + 1) * cell, std::max(int(film->items.size()), 1) * cell),
			QImage::Format_ARGB32_Premultiplied);
		film->image.fill(QColor(0x1c, 0x1c, 0x1c));
		film->scratch = QImage(QSize(cell, cell), QImage::Format_ARGB32_Premultiplied);
		const auto timer = new QTimer(qApp);
		QObject::connect(timer, &QTimer::timeout, [=] {
			const auto now = crl::now();
			const auto tick = film->tick++;
			const auto column = (tick >= 40) ? ((tick - 40) / 15) : -1;
			const auto capture = (tick >= 40) && ((tick - 40) % 15 == 0);
			auto p = QPainter(capture ? &film->image : &film->scratch);
			for (auto i = 0; i != int(film->items.size()); ++i) {
				const auto y = i * cell;
				if (capture && column == 0) {
					p.setPen(QColor(0x8e, 0x8e, 0x93));
					p.drawText(
						QRect(0, y, cell, cell),
						Qt::AlignCenter,
						QString::number(film->indices[i]));
				}
				film->items[i]->paint(p, {
					.textColor = QColor(255, 255, 255),
					.now = now,
					.position = capture
						? QPoint((column + 1) * cell + 4, y + 4)
						: QPoint(4, 4),
				});
			}
			p.end();
			if (column >= frames - 1 && capture) {
				timer->stop();
				timer->deleteLater();
				film->image.save(path);
			}
		});
		timer->start(16);
		return true;
	} else if (cmd == u"clawd:on"_q || cmd == u"clawd:off"_q) {
		Mokagram::Settings::SetBool(
			QString::fromUtf8(kEnabledKey),
			cmd == u"clawd:on"_q);
		return true;
	} else if (cmd.startsWith(u"clawd:"_q)) {
		for (const auto &[widget, clawd] : Alive()) {
			if (widget->isVisible()) {
				clawd->act(cmd.mid(6));
			}
		}
		return true;
	}
	return false;
}

} // namespace Mokagram::Pet

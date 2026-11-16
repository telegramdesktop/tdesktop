// This file is part of mokaGram, https://github.com/ecronxy/mokagram
// (c) 2026 @ecronx (https://t.me/ecronx). GPLv3 with the attribution term
// of MOKAGRAM_NOTICE.md (section 7(b)): keep this notice when copying.
// mokagram-id: 5d82b513
#include "mokagram/effects/mokagram_effects.h"
#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/misc/mokagram_misc.h"

#include "mokagram/settings/mokagram_settings.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "media/clip/media_clip_reader.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "core/file_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/widgets/fields/input_field.h"

#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QRandomGenerator>
#include <QtGui/QGuiApplication>
#include <QtGui/QPixmap>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>
#include <QtWidgets/QTextEdit>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

namespace Mokagram::Effects {
namespace {

namespace S = Settings;

constexpr auto kDelEnabled = "fx.delete.enabled";
constexpr auto kDelStyle = "fx.delete.style";
constexpr auto kDelDuration = "fx.delete.duration";
constexpr auto kDelSize = "fx.delete.size";
constexpr auto kDelSpeed = "fx.delete.speed";
constexpr auto kDelMax = "fx.delete.max_particles";
constexpr auto kDelOwn = "fx.delete.only_own";
constexpr auto kDelColor = "fx.delete.color";

constexpr auto kWallpaperEnabled = "look.wallpaper.enabled";
constexpr auto kWallpaperFile = "look.wallpaper.file";
constexpr auto kTypEnabled = "fx.typing.enabled";
constexpr auto kTypStyle = "fx.typing.style";
constexpr auto kTypCount = "fx.typing.count";
constexpr auto kTypLife = "fx.typing.life";
constexpr auto kTypSize = "fx.typing.size";
constexpr auto kTypSpread = "fx.typing.spread";
constexpr auto kTypRainbow = "fx.typing.rainbow";
constexpr auto kTypColor = "fx.typing.color";
constexpr auto kTypMax = "fx.typing.max_particles";

enum class DeleteStyle { Shards, Dynamite, Portal, Dust };
enum class TypingStyle { Sparks, Stars, Hearts, Confetti };

constexpr auto kMaxBursts = 6;
constexpr auto kHueBuckets = 12;
constexpr auto kGravity = 900.;

[[nodiscard]] double Rand() {
	return QRandomGenerator::global()->generateDouble();
}

[[nodiscard]] double Rand(double a, double b) {
	return a + (b - a) * Rand();
}

[[nodiscard]] double Clamp01(double v) {
	return std::clamp(v, 0., 1.);
}

[[nodiscard]] QColor ParseColor(const QString &text, QColor fallback) {
	const auto trimmed = text.trimmed();
	if (trimmed.isEmpty()) {
		return fallback;
	}
	const auto result = QColor(trimmed);
	return result.isValid() ? result : fallback;
}

struct Tile {
	QRectF src;      // в пикселях картинки
	QPointF center;  // исходный центр в логических пикселях
	QPointF size;
	QPointF vel;
	double spin = 0.;
	double delay = 0.;
	double angle0 = 0.;
};

struct Smoke {
	QPointF pos;
	QPointF vel;
	double radius = 0.;
	double delay = 0.;
};

struct Burst {
	QImage image;
	QRectF area; // в координатах canvas
	DeleteStyle style = DeleteStyle::Shards;
	crl::time start = 0;
	int duration = 800;
	QColor accent;
	std::vector<Tile> tiles;
	std::vector<Smoke> smoke;
};

struct Spark {
	QPointF pos;
	QPointF vel;
	double rotation = 0.;
	double spin = 0.;
	double size = 8.;
	crl::time start = 0;
	int life = 700;
	int sprite = 0;
};

struct SpriteKey {
	int style, hue, size, dpr;
	quint32 rgba;
	friend bool operator<(const SpriteKey &a, const SpriteKey &b) {
		return std::tie(a.style, a.hue, a.size, a.dpr, a.rgba)
			< std::tie(b.style, b.hue, b.size, b.dpr, b.rgba);
	}
};

[[nodiscard]] QPainterPath StarPath(double r) {
	auto path = QPainterPath();
	for (auto i = 0; i != 10; ++i) {
		const auto radius = (i % 2) ? r * 0.42 : r;
		const auto a = -M_PI / 2 + i * M_PI / 5;
		const auto pt = QPointF(radius * std::cos(a), radius * std::sin(a));
		i ? path.lineTo(pt) : path.moveTo(pt);
	}
	path.closeSubpath();
	return path;
}

[[nodiscard]] QPainterPath HeartPath(double r) {
	auto path = QPainterPath();
	path.moveTo(0, r * 0.9);
	path.cubicTo(-r * 1.5, -r * 0.1, -r * 0.7, -r * 1.1, 0, -r * 0.35);
	path.cubicTo(r * 0.7, -r * 1.1, r * 1.5, -r * 0.1, 0, r * 0.9);
	return path;
}

class Canvas final : public Ui::RpWidget {
public:
	explicit Canvas(QWidget *host) : RpWidget(host) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setGeometry(host->rect());
		host->installEventFilter(this);
		_animation.init([=](crl::time now) {
			return step(now);
		});
		show();
	}

	[[nodiscard]] static Canvas *For(not_null<QWidget*> host) {
		auto result = static_cast<Canvas*>(nullptr);
		for (const auto child : host->children()) {
			if ((result = dynamic_cast<Canvas*>(child))) {
				break;
			}
		}
		if (!result) {
			result = new Canvas(host);
		}
		return result;
	}

	[[nodiscard]] bool burstsFull() const {
		return int(_bursts.size()) >= kMaxBursts;
	}

	void addBurst(Burst burst) {
		burst.start = crl::now();
		_bursts.push_back(std::move(burst));
		wake();
	}

	void addSparks(
			QPointF at,
			TypingStyle style,
			int count,
			int life,
			int size,
			double spread,
			bool rainbow,
			QColor color,
			int maxTotal) {
		const auto now = crl::now();
		if (const auto overflow = int(_sparks.size()) + count - maxTotal; overflow > 0) {
			const auto toRemove = std::min(overflow, int(_sparks.size()));
			_sparks.erase(_sparks.begin(), _sparks.begin() + toRemove);
		}
		for (auto i = 0; i != count; ++i) {
			auto s = Spark();
			s.pos = at + QPointF(Rand(-3, 3), Rand(-2, 2));
			const auto angle = Rand(-M_PI * 0.92, -M_PI * 0.08);
			const auto speed = Rand(70, 200) * spread;
			s.vel = QPointF(std::cos(angle), std::sin(angle)) * speed;
			s.rotation = Rand(0, 360);
			s.spin = Rand(-300, 300);
			s.size = size * Rand(0.7, 1.25);
			s.start = now;
			s.life = int(life * Rand(0.8, 1.2));
			const auto hue = (rainbow || style == TypingStyle::Confetti)
				? int(Rand(0, kHueBuckets))
				: -1;
			s.sprite = spriteIndex(style, hue, int(std::round(s.size)), color);
			_sparks.push_back(s);
			_gravityScale = spread;
		}
		wake();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		if (_bursts.empty() && _sparks.empty()) {
			return;
		}
		auto p = QPainter(this);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.setRenderHint(QPainter::Antialiasing);
		const auto now = crl::now();
		for (const auto &burst : _bursts) {
			paintBurst(p, burst, now);
		}
		paintSparks(p, now);
	}

	bool eventFilter(QObject *o, QEvent *e) override {
		if (e->type() == QEvent::Resize) {
			setGeometry(parentWidget()->rect());
		}
		return false;
	}

private:
	void wake() {
		raise();
		if (!_animation.animating()) {
			_animation.start();
		}
	}

	bool step(crl::time now) {
		_bursts.erase(
			std::remove_if(_bursts.begin(), _bursts.end(), [&](auto &b) {
				return now - b.start >= b.duration;
			}),
			_bursts.end());
		_sparks.erase(
			std::remove_if(_sparks.begin(), _sparks.end(), [&](auto &s) {
				return now - s.start >= s.life;
			}),
			_sparks.end());
		update();
		return !_bursts.empty() || !_sparks.empty();
	}

	void paintBurst(QPainter &p, const Burst &b, crl::time now) {
		const auto T = Clamp01((now - b.start) / double(b.duration));
		const auto sec = (now - b.start) / 1000.;
		const auto center = b.area.center();
		const auto drawTile = [&](const Tile &t, QPointF pos, double angle, double scale, double alpha) {
			if (alpha <= 0.01 || scale <= 0.01) {
				return;
			}
			p.setOpacity(alpha);
			p.translate(pos);
			p.rotate(angle);
			p.scale(scale, scale);
			p.drawImage(
				QRectF(-t.size.x() / 2, -t.size.y() / 2, t.size.x(), t.size.y()),
				b.image,
				t.src);
			p.resetTransform();
		};
		const auto origin = b.area.topLeft();
		switch (b.style) {
		case DeleteStyle::Shards:
			for (const auto &t : b.tiles) {
				const auto lt = std::max(0., sec - t.delay);
				const auto fade = Clamp01((1. - T) / 0.4);
				const auto pos = origin + t.center + t.vel * lt
					+ QPointF(0, 0.5 * kGravity * lt * lt);
				drawTile(t, pos, t.angle0 + t.spin * lt, 1., fade);
			}
			break;
		case DeleteStyle::Dynamite: {
			const auto boom = 0.16;
			if (T < boom) {
				const auto k = T / boom;
				const auto dx = std::sin(k * 60.) * 2.5 * k;
				const auto dy = std::cos(k * 50.) * 1.5 * k;
				p.setOpacity(1.);
				p.drawImage(
					QRectF(b.area).translated(dx, dy),
					b.image);
				// Тлеющий запал: красноватый налёт.
				p.setOpacity(0.25 * k);
				p.fillRect(b.area, b.accent);
				break;
			}
			const auto u = (T - boom) / (1. - boom);
			const auto lt = std::max(0., sec - boom * b.duration / 1000.);
			for (const auto &t : b.tiles) {
				const auto fade = Clamp01((1. - u) / 0.5);
				const auto pos = origin + t.center + t.vel * lt
					+ QPointF(0, 0.5 * kGravity * 0.6 * lt * lt);
				drawTile(t, pos, t.angle0 + t.spin * lt, 1. - u * 0.4, fade);
			}
			for (const auto &s : b.smoke) {
				const auto k = Clamp01((u - s.delay) / (1. - s.delay));
				if (k <= 0) {
					continue;
				}
				const auto r = s.radius * (0.4 + k);
				auto g = QRadialGradient(origin + s.pos + s.vel * k, r);
				auto c = QColor(70, 70, 70);
				c.setAlphaF(0.45 * (1. - k));
				g.setColorAt(0, c);
				c.setAlpha(0);
				g.setColorAt(1, c);
				p.setOpacity(1.);
				p.setPen(Qt::NoPen);
				p.setBrush(g);
				p.drawEllipse(origin + s.pos + s.vel * k, r, r);
			}
			// Вспышка.
			const auto flash = Clamp01(1. - u / 0.3);
			if (flash > 0) {
				const auto r = std::max(b.area.width(), b.area.height()) * (0.35 + 0.5 * u);
				auto g = QRadialGradient(center, r);
				auto c = b.accent.lighter(150);
				c.setAlphaF(0.85 * flash);
				g.setColorAt(0, c);
				c.setAlpha(0);
				g.setColorAt(1, c);
				p.setOpacity(1.);
				p.setPen(Qt::NoPen);
				p.setBrush(g);
				p.drawEllipse(center, r, r);
			}
		} break;
		case DeleteStyle::Portal: {
			const auto rx = std::max(18., b.area.width() * 0.16);
			const auto ry = std::max(26., std::min(b.area.height() * 0.62, 90.));
			const auto grow = Clamp01(T / 0.15);
			const auto ringFade = T > 0.8 ? Clamp01((1. - T) / 0.2) : 1.;
			const auto sx = rx * grow, sy = ry * grow;
			// Воронка: сам портал под частицами.
			for (auto i = 3; i >= 0; --i) {
				auto c = b.accent;
				c.setAlphaF(0.22 * ringFade);
				p.setOpacity(1.);
				p.setPen(QPen(c, 2 + i * 3));
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(center, sx + i, sy + i);
			}
			auto g = QRadialGradient(center, std::max(sx, sy));
			auto in = b.accent.darker(400);
			in.setAlphaF(0.85 * ringFade);
			g.setColorAt(0, in);
			in.setAlpha(0);
			g.setColorAt(1, in);
			p.setPen(Qt::NoPen);
			p.setBrush(g);
			p.drawEllipse(center, sx, sy);
			for (const auto &t : b.tiles) {
				const auto k = Clamp01((T - 0.1 - t.delay) / (0.9 - t.delay - 0.05));
				const auto e = k * k * (3 - 2 * k);
				const auto from = origin + t.center;
				const auto rel = from - center;
				const auto swirl = e * 5.5 * (1. - e * 0.3);
				const auto cs = std::cos(swirl), sn = std::sin(swirl);
				const auto rotated = QPointF(
					rel.x() * cs - rel.y() * sn,
					rel.x() * sn + rel.y() * cs) * (1. - e);
				drawTile(
					t,
					center + rotated,
					t.angle0 + e * 540,
					1. - e,
					Clamp01((1. - e) / 0.6));
			}
		} break;
		case DeleteStyle::Dust:
			for (const auto &t : b.tiles) {
				const auto k = Clamp01((T - t.delay) / (1. - t.delay));
				if (k <= 0) {
					p.setOpacity(1.);
					p.drawImage(
						QRectF(origin + t.center - QPointF(t.size.x() / 2, t.size.y() / 2), QSizeF(t.size.x(), t.size.y())),
						b.image,
						t.src);
					continue;
				}
				const auto pos = origin + t.center + t.vel * (k * b.duration / 1000.)
					+ QPointF(std::sin(k * 7. + t.angle0) * 6., 0.);
				drawTile(t, pos, t.angle0 * 0.1, 1. - 0.5 * k, 1. - k);
			}
			break;
		}
		p.setOpacity(1.);
	}

	[[nodiscard]] int spriteIndex(TypingStyle style, int hue, int size, QColor color) {
		const auto key = SpriteKey{
			int(style),
			hue,
			size,
			int(devicePixelRatioF() * 100),
			(hue >= 0) ? quint32(0) : quint32(color.rgba()),
		};
		auto i = _spriteIndex.find(key);
		if (i != _spriteIndex.end()) {
			return i->second;
		}
		const auto dpr = devicePixelRatioF();
		const auto side = int(std::ceil(size * 2 + 4));
		auto image = QImage(
			QSize(side, side) * dpr,
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(dpr);
		image.fill(Qt::transparent);
		{
			auto q = QPainter(&image);
			q.setRenderHint(QPainter::Antialiasing);
			q.translate(side / 2., side / 2.);
			auto c = (hue >= 0)
				? QColor::fromHsv(int(hue * 360. / kHueBuckets), 190, 255)
				: color;
			q.setPen(Qt::NoPen);
			const auto r = double(size);
			switch (style) {
			case TypingStyle::Sparks: {
				auto g = QRadialGradient(QPointF(0, 0), r);
				auto hot = c.lighter(160);
				g.setColorAt(0, hot);
				g.setColorAt(0.35, c);
				auto edge = c;
				edge.setAlpha(0);
				g.setColorAt(1, edge);
				q.setBrush(g);
				q.drawEllipse(QPointF(0, 0), r, r);
			} break;
			case TypingStyle::Stars:
				q.setBrush(c);
				q.drawPath(StarPath(r));
				break;
			case TypingStyle::Hearts:
				q.setBrush(c);
				q.drawPath(HeartPath(r));
				break;
			case TypingStyle::Confetti:
				q.setBrush(c);
				q.drawRoundedRect(QRectF(-r * 0.6, -r * 0.3, r * 1.2, r * 0.6), 1.5, 1.5);
				break;
			}
		}
		const auto index = int(_sprites.size());
		_sprites.push_back(std::move(image));
		_spriteIndex.emplace(key, index);
		return index;
	}

	void paintSparks(QPainter &p, crl::time now) {
		for (const auto &s : _sparks) {
			const auto T = Clamp01((now - s.start) / double(s.life));
			const auto sec = (now - s.start) / 1000.;
			const auto pos = s.pos + s.vel * sec
				+ QPointF(0, 0.5 * 380. * _gravityScale * sec * sec);
			const auto scale = T < 0.15 ? T / 0.15 : (1. - (T - 0.15) / 0.85 * 0.5);
			const auto &img = _sprites[s.sprite];
			p.setOpacity(std::pow(1. - T, 0.8));
			p.translate(pos);
			p.rotate(s.rotation + s.spin * sec);
			p.scale(scale, scale);
			const auto side = img.width() / img.devicePixelRatio();
			p.drawImage(QRectF(-side / 2, -side / 2, side, side), img);
			p.resetTransform();
		}
		p.setOpacity(1.);
	}

	std::vector<Burst> _bursts;
	std::vector<Spark> _sparks;
	std::vector<QImage> _sprites;
	std::map<SpriteKey, int> _spriteIndex;
	double _gravityScale = 1.;
	Ui::Animations::Basic _animation;

};

[[nodiscard]] bool Skip() {
	return anim::Disabled()
		|| PowerSaving::On(PowerSaving::kChatEffects)
		|| PowerSaving::On(PowerSaving::kAnimations);
}

void BuildTiles(Burst &b, int cell, int maxTiles, double speed) {
	const auto dpr = b.image.devicePixelRatio();
	const auto w = b.area.width();
	const auto h = b.area.height();
	auto step = double(std::max(cell, 4));
	while (std::ceil(w / step) * std::ceil(h / step) > maxTiles) {
		step *= 1.12;
	}
	const auto cols = int(std::ceil(w / step));
	const auto rows = int(std::ceil(h / step));
	const auto cx = w / 2, cy = h / 2;
	const auto img = b.image;
	const auto opaque = [&](QRectF src) {
		const auto x0 = int(src.left()), x1 = std::min(img.width() - 1, int(src.right()));
		const auto y0 = int(src.top()), y1 = std::min(img.height() - 1, int(src.bottom()));
		const auto sx = std::max(1, (x1 - x0) / 2), sy = std::max(1, (y1 - y0) / 2);
		for (auto y = y0; y <= y1; y += sy) {
			const auto line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
			for (auto x = x0; x <= x1; x += sx) {
				if (qAlpha(line[x]) > 8) {
					return true;
				}
			}
		}
		return false;
	};
	b.tiles.reserve(cols * rows);
	for (auto r = 0; r != rows; ++r) {
		for (auto c = 0; c != cols; ++c) {
			auto t = Tile();
			const auto x = c * step, y = r * step;
			const auto tw = std::min(step, w - x), th = std::min(step, h - y);
			t.src = QRectF(x * dpr, y * dpr, tw * dpr, th * dpr);
			if (!opaque(t.src)) {
				continue;
			}
			t.center = QPointF(x + tw / 2, y + th / 2);
			t.size = QPointF(tw, th);
			const auto dx = t.center.x() - cx, dy = t.center.y() - cy;
			const auto dist = std::max(1., std::hypot(dx, dy));
			const auto nx = dx / dist, ny = dy / dist;
			t.angle0 = Rand(-12, 12);
			switch (b.style) {
			case DeleteStyle::Shards: {
				const auto v = Rand(120, 380) * speed;
				t.vel = QPointF(nx * v + Rand(-60, 60), ny * v - Rand(80, 260) * speed);
				t.spin = Rand(-420, 420) * speed;
				t.delay = Rand(0, 0.08);
			} break;
			case DeleteStyle::Dynamite: {
				const auto v = Rand(260, 760) * speed;
				t.vel = QPointF(nx * v, ny * v - Rand(60, 300) * speed);
				t.spin = Rand(-720, 720) * speed;
			} break;
			case DeleteStyle::Portal:
				t.delay = Clamp01(dist / std::max(1., std::hypot(cx, cy))) * 0.28
					+ Rand(0, 0.06);
				break;
			case DeleteStyle::Dust:
				t.vel = QPointF(Rand(30, 140) * speed, Rand(-90, -20) * speed);
				t.delay = Clamp01(t.center.x() / std::max(1., w)) * 0.55
					+ Rand(0, 0.1);
				t.angle0 = Rand(0, 6.28);
				break;
			}
			b.tiles.push_back(t);
		}
	}
	if (b.style == DeleteStyle::Dynamite) {
		for (auto i = 0; i != 9; ++i) {
			auto s = Smoke();
			s.pos = QPointF(cx + Rand(-w * 0.3, w * 0.3), cy + Rand(-h * 0.3, h * 0.3));
			s.vel = QPointF(Rand(-40, 40), Rand(-70, -10));
			s.radius = Rand(24, 54);
			s.delay = Rand(0, 0.25);
			b.smoke.push_back(s);
		}
	}
}

} // namespace

void RegisterSettings() {
	const auto del = QString::fromUtf8(Mokagram::Lang::Tr("Эффект удаления сообщений"));
	const auto add = [](S::Entry entry, const QString &section, const char *icon) {
		entry.section = section;
		entry.category = u"look"_q;
		entry.sectionIcon = QString::fromUtf8(icon);
		S::Register(std::move(entry));
	};
	const auto d = [&](S::Entry e) { add(std::move(e), del, "trash"); };
	const auto U = [](const char *s) { return QString::fromUtf8(s); };
	add({
		.key = QString::fromUtf8(kWallpaperEnabled),
		.title = U(Mokagram::Lang::Tr("Анимированные обои")),
		.description = U(Mokagram::Lang::Tr("Гиф или видео из файла играет вместо фона чата. Пауза при энергосбережении.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	}, U(Mokagram::Lang::Tr("Обои")), "palette");
	add({
		.key = u"look.wallpaper.pick"_q,
		.title = U(Mokagram::Lang::Tr("Файл обоев")),
		.description = U(Mokagram::Lang::Tr("GIF, MP4 или WebM. Выберите свой файл.")),
		.kind = S::Kind::String,
		.defaultValue = U(Mokagram::Lang::Tr("Выбрать")),
		.dependsOn = QString::fromUtf8(kWallpaperEnabled),
		.action = [] {
			const auto window = Core::App().activePrimaryWindow();
			if (!window) {
				return;
			}
			FileDialog::GetOpenPath(
				window->widget().get(),
				QString::fromUtf8(Mokagram::Lang::Tr("Файл обоев")),
				QString::fromUtf8(Mokagram::Lang::Tr("Анимация (*.gif *.mp4 *.webm *.mov);;"))
					+ FileDialog::AllFilesFilter(),
				crl::guard(window->widget().get(), [](FileDialog::OpenResult &&result) {
					if (!result.paths.isEmpty()) {
						S::SetString(
							QString::fromUtf8(kWallpaperFile),
							result.paths.front());
					}
				}));
		},
	}, U(Mokagram::Lang::Tr("Обои")), "palette");
	S::Register({
		.key = QString::fromUtf8(kWallpaperFile),
		.kind = S::Kind::String,
		.defaultValue = QString(),
		.hidden = true,
	});

	add({
		.key = u"fx.startup.enabled"_q,
		.title = U(Mokagram::Lang::Tr("Заставка при запуске")),
		.description = U(Mokagram::Lang::Tr("Знак mokaGram проявляется и растворяется при открытии окна.")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
	}, U(Mokagram::Lang::Tr("Запуск")), "palette");


	d({
		.key = u"fx.delete.enabled"_q,
		.title = U(Mokagram::Lang::Tr("Рассыпать сообщение при удалении")),
		.description = U(Mokagram::Lang::Tr("Удалённое сообщение превращается в частицы.")),
		.kind = S::Kind::Bool,
		.defaultValue = true,
		.sectionNote = U(Mokagram::Lang::Tr("Рисуется на лету без нагрузки на чат, а число частиц ограничено.")),
	});
	d({
		.key = u"fx.delete.style"_q,
		.title = U(Mokagram::Lang::Tr("Эффект")),
		.kind = S::Kind::Choice,
		.defaultValue = 0,
		.choices = { U(Mokagram::Lang::Tr("Осколки")), U(Mokagram::Lang::Tr("Динамит")), U(Mokagram::Lang::Tr("Портал")), U(Mokagram::Lang::Tr("Пыль")) },
		.dependsOn = u"fx.delete.enabled"_q,
	});
	d({
		.key = u"fx.delete.duration"_q,
		.title = U(Mokagram::Lang::Tr("Длительность, мс")),
		.kind = S::Kind::Int,
		.defaultValue = 450,
		.minValue = 300,
		.maxValue = 2500,
		.dependsOn = u"fx.delete.enabled"_q,
	});
	d({
		.key = u"fx.delete.size"_q,
		.title = U(Mokagram::Lang::Tr("Размер частиц, px")),
		.description = U(Mokagram::Lang::Tr("Меньше: частиц больше. Число автоматически ограничено.")),
		.kind = S::Kind::Int,
		.defaultValue = 16,
		.minValue = 6,
		.maxValue = 40,
		.dependsOn = u"fx.delete.enabled"_q,
	});
	d({
		.key = u"fx.delete.speed"_q,
		.title = U(Mokagram::Lang::Tr("Скорость разлёта, %")),
		.kind = S::Kind::Int,
		.defaultValue = 100,
		.minValue = 30,
		.maxValue = 300,
		.dependsOn = u"fx.delete.enabled"_q,
	});
	d({
		.key = u"fx.delete.max_particles"_q,
		.title = U(Mokagram::Lang::Tr("Максимум частиц")),
		.description = U(Mokagram::Lang::Tr("Предел на одно сообщение. Для слабых компьютеров ставь меньше.")),
		.kind = S::Kind::Int,
		.defaultValue = 48,
		.minValue = 30,
		.maxValue = 500,
		.dependsOn = u"fx.delete.enabled"_q,
	});
	d({
		.key = u"fx.delete.only_own"_q,
		.title = U(Mokagram::Lang::Tr("Только мои сообщения")),
		.kind = S::Kind::Bool,
		.defaultValue = false,
		.dependsOn = u"fx.delete.enabled"_q,
	});
	d({
		.key = u"fx.delete.color"_q,
		.title = U(Mokagram::Lang::Tr("Цвет вспышки и портала")),
		.description = U(Mokagram::Lang::Tr("Для динамита и портала. Пусто: цвет по умолчанию.")),
		.kind = S::Kind::Color,
		.defaultValue = QString(),
		.dependsOn = u"fx.delete.enabled"_q,
	});
}

void PlayDelete(
		not_null<QWidget*> host,
		not_null<QWidget*> source,
		QRect rectInSource,
		bool outgoing) {
	if (Skip()
		|| !S::Bool(QString::fromUtf8(kDelEnabled))
		|| (S::Bool(QString::fromUtf8(kDelOwn)) && !outgoing)
		|| rectInSource.isEmpty()) {
		return;
	}
	const auto canvas = Canvas::For(host);
	if (canvas->burstsFull()) {
		return;
	}
	auto pixmap = source->grab(rectInSource);
	auto image = pixmap.toImage().convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	const auto dpr = image.devicePixelRatio();
	// grab() заливает фон плоским цветом: считаем его прозрачным по углу.
	{
		const auto bg = image.pixel(0, 0);
		const auto sameColor = [](int a, int b) { return std::abs(a - b) <= 2; };
		for (auto y = 0; y != image.height(); ++y) {
			const auto line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (auto x = 0; x != image.width(); ++x) {
				if (sameColor(qRed(line[x]), qRed(bg))
					&& sameColor(qGreen(line[x]), qGreen(bg))
					&& sameColor(qBlue(line[x]), qBlue(bg))) {
					line[x] = 0;
				}
			}
		}
	}
	// Обрезаем прозрачные поля, чтобы рассыпался только пузырь.
	auto minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
	for (auto y = 0; y != image.height(); ++y) {
		const auto line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (auto x = 0; x != image.width(); ++x) {
			if (qAlpha(line[x]) > 8) {
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}
	}
	if (maxX < 0) {
		return;
	}
	const auto crop = QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
	auto burst = Burst();
	burst.image = image.copy(crop);
	burst.image.setDevicePixelRatio(dpr);
	burst.style = DeleteStyle(std::clamp(
		S::Int(QString::fromUtf8(kDelStyle)),
		0,
		3));
	burst.duration = std::clamp(S::Int(QString::fromUtf8(kDelDuration)), 300, 2500);
	const auto topLeft = host->mapFromGlobal(
		source->mapToGlobal(rectInSource.topLeft()));
	burst.area = QRectF(
		topLeft.x() + crop.x() / dpr,
		topLeft.y() + crop.y() / dpr,
		crop.width() / dpr,
		crop.height() / dpr);
	burst.accent = ParseColor(
		S::String(QString::fromUtf8(kDelColor)),
		(burst.style == DeleteStyle::Portal)
			? QColor(138, 92, 255)
			: QColor(255, 176, 43));
	BuildTiles(
		burst,
		S::Int(QString::fromUtf8(kDelSize)),
		std::clamp(S::Int(QString::fromUtf8(kDelMax)), 30, 500),
		std::clamp(S::Int(QString::fromUtf8(kDelSpeed)), 30, 300) / 100.);
	if (burst.tiles.empty()) {
		return;
	}
	canvas->addBurst(std::move(burst));
}


namespace {

// Накрывает окно фоном клиента, пока знак проявляется, затем плавно растворяется.
class StartupOverlay final : public Ui::RpWidget {
public:
	StartupOverlay(not_null<QWidget*> host)
	: RpWidget(host) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setGeometry(host->rect());
		host->installEventFilter(this);
		// Всегда наш знак, а не значок окна: он может быть стандартным телеграмовским.
		_logo = QPixmap::fromImage(Misc::AppIconImage(1));
		show();
		raise();
		_animation.start([=] {
			update();
			if (!_animation.animating()) {
				deleteLater();
			}
		}, 0., 1., 900, anim::linear);
	}

protected:
	bool eventFilter(QObject *o, QEvent *e) override {
		if (e->type() == QEvent::Resize) {
			setGeometry(parentWidget()->rect());
		}
		return false;
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto t = _animation.value(1.);
		// 0..0.35: знак въезжает, 0.35..1: всё растворяется.
		const auto appear = anim::easeOutCubic(1., std::clamp(t / 0.35, 0., 1.));
		const auto fade = 1. - anim::easeOutCubic(1., std::clamp((t - 0.35) / 0.65, 0., 1.));
		p.setOpacity(fade);
		p.fillRect(rect(), QColor(0x1c, 0x1c, 0x1c));
		if (_logo.isNull()) {
			return;
		}
		const auto size = style::ConvertScale(96);
		const auto scale = (0.8 + 0.2 * appear) + 0.12 * (1. - fade);
		p.translate(rect().center());
		p.scale(scale, scale);
		p.setOpacity(fade * appear);
		// Знак круглый, как аватарка.
		auto circle = QPainterPath();
		circle.addEllipse(QRect(-size / 2, -size / 2, size, size));
		p.setClipPath(circle);
		p.drawPixmap(
			QRect(-size / 2, -size / 2, size, size),
			_logo);
	}

private:
	QPixmap _logo;
	Ui::Animations::Simple _animation;

};

} // namespace

void PlayStartup(not_null<QWidget*> host) {
	if (!S::Bool(u"fx.startup.enabled"_q)) {
		return;
	}
	Ui::CreateChild<StartupOverlay>(host.get());
}


namespace {

struct WallpaperState {
	QString path;
	Media::Clip::ReaderPointer reader;
	QPointer<QWidget> widget;
	QSize frame;
	bool bad = false;
	bool subscribed = false;
	rpl::lifetime lifetime;
};

[[nodiscard]] WallpaperState &Wallpaper() {
	static auto result = WallpaperState();
	return result;
}

void WallpaperRepaint() {
	if (const auto widget = Wallpaper().widget.data()) {
		widget->update();
	}
}

void WallpaperReset() {
	auto &st = Wallpaper();
	st.reader = nullptr;
	st.frame = QSize();
	st.bad = false;
	st.path = S::Bool(QString::fromUtf8(kWallpaperEnabled))
		? S::String(QString::fromUtf8(kWallpaperFile))
		: QString();
	WallpaperRepaint();
}

void WallpaperEnsureReader() {
	auto &st = Wallpaper();
	if (st.reader || st.bad || st.path.isEmpty()) {
		return;
	}
	if (!QFileInfo::exists(st.path)) {
		st.bad = true;
		return;
	}
	st.reader = Media::Clip::MakeReader(st.path, [](Media::Clip::Notification n) {
		auto &st = Wallpaper();
		if (n == Media::Clip::Notification::Reinit) {
			if (!st.reader || st.reader->state() == Media::Clip::State::Error) {
				st.bad = true;
				st.reader = nullptr;
			}
		}
		WallpaperRepaint();
	});
}

} // namespace

void WallpaperAttach(not_null<QWidget*> widget) {
	auto &st = Wallpaper();
	st.widget = widget.get();
	if (!st.subscribed) {
		st.subscribed = true;
		S::Changed() | rpl::filter([](const QString &key) {
			return key == QString::fromUtf8(kWallpaperEnabled)
				|| key == QString::fromUtf8(kWallpaperFile);
		}) | rpl::on_next([] {
			WallpaperReset();
		}, st.lifetime);
		WallpaperReset();
	}
}

bool PaintWallpaper(QPainter &p, QSize fill, QRect clip, bool paused) {
	auto &st = Wallpaper();
	if (st.path.isEmpty() || st.bad || fill.isEmpty()) {
		return false;
	}
	WallpaperEnsureReader();
	if (!st.reader || !st.reader->ready()) {
		return false;
	}
	const auto original = QSize(st.reader->width(), st.reader->height());
	if (original.isEmpty()) {
		return false;
	}
	// Окно в фоне: кадр замирает, декодер не работает впустую.
	paused = paused || (QGuiApplication::applicationState() != Qt::ApplicationActive);
	// По заполнению: кадр масштабируется так, чтобы закрыть весь фон,
	// но декодируется не крупнее 1280 по длинной стороне: остальное растянет отрисовка.
	constexpr auto kMaxSide = 1280;
	auto target = fill;
	if (std::max(target.width(), target.height()) > kMaxSide) {
		target = target.scaled(
			QSize(kMaxSide, kMaxSide),
			Qt::KeepAspectRatio);
	}
	const auto frame = original.scaled(target, Qt::KeepAspectRatioByExpanding);
	if (!st.reader->started()) {
		st.frame = frame;
		st.reader->start({ .frame = frame, .outer = frame });
		return false;
	}
	const auto image = st.reader->current(
		{ .frame = st.frame, .outer = st.frame },
		paused ? 0 : crl::now());
	if (image.isNull()) {
		return false;
	}
	// Растягиваем до размера фона, если декодировали меньше.
	const auto cover = QSizeF(image.size() / image.devicePixelRatio())
		.scaled(QSizeF(fill), Qt::KeepAspectRatioByExpanding);
	p.save();
	p.setClipRect(clip);
	p.drawImage(
		QRectF(
			(fill.width() - cover.width()) / 2.,
			(fill.height() - cover.height()) / 2.,
			cover.width(),
			cover.height()),
		image);
	p.restore();
	return true;
}

} // namespace Mokagram::Effects

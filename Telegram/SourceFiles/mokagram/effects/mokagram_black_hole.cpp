/*
This file is part of mokaGram, a Telegram Desktop fork.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mokagram/effects/mokagram_black_hole.h"

#include "core/application.h"
#include "data/data_peer.h"
#include "history/history_widget.h"
#include "mainwidget.h"
#include "ui/effects/animations.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "base/event_filter.h"
#include "base/weak_ptr.h"
#include "base/debug_log.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

// Очистка истории как чёрная дыра из островка: островок раскрывается в
// дыру с кольцом света, обои закручиваются к ней, сообщения затягивает по
// дуге с вытягиванием у горизонта, потом дыра садится обратно в островок.
// Тайминг и формулы перенесены с iOS-прототипа BlackHoleDelete, а
// шейдеры переписаны на процессор: клиент может работать без OpenGL.

namespace Mokagram::BlackHole {
namespace {

constexpr auto kPi = 3.14159265358979323846;
constexpr auto kMaxStep = 1. / 30.;
constexpr auto kFadeOut = 0.14;

int Active = 0;

[[nodiscard]] double Clamp01(double x) {
	return std::clamp(x, 0., 1.);
}

[[nodiscard]] double Smooth(double a, double b, double x) {
	const auto t = Clamp01((x - a) / ((b - a) == 0. ? 0.0001 : (b - a)));
	return t * t * (3. - 2. * t);
}

[[nodiscard]] double InOut(double x) {
	return x * x * (3. - 2. * x);
}

[[nodiscard]] double OutCubic(double x) {
	return 1. - std::pow(1. - x, 3.);
}

[[nodiscard]] double OutQuint(double x) {
	return 1. - std::pow(1. - x, 5.);
}

// S-образный старт без рывка и длинный хвост.
[[nodiscard]] double Soft(double x) {
	return OutQuint(InOut(x)) * 0.6 + InOut(InOut(x)) * 0.4;
}

[[nodiscard]] double Lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

struct Config {
	double holeRadius = 0.;
	double emergeDuration = 0.5;
	double collapseDuration = 0.22;
	double collapseBounce = 0.35;
	double settleDuration = 0.26;

	double ringBrightness = 1.03;
	double ringSpeed = 2.0;
	double ringSpin = 0.35;
	double innerBoost = 4.5;
	double innerWidth = 0.08;
	double ringSqueeze = 0.7;
	double ringGamma = 0.75;

	double glow = 0.35;
	double lensStrength = 1.5;
	double spaceDarkness = 0.55;
	double spaceSize = 2.0;
	double backgroundSwirl = 0.9;
	double backgroundSpin = 0.8;

	double swallowLead = 0.1;
	double swallowDuration = 0.7;
	double arcShape = 1.0;
	double pullStrength = 3.;
	double pullReach = 0.8;
	double flowRamp = 0.2;
	double flowAccel = 8.;
	double sinkDepth = 0.;
	double fadeOuter = 1.6;
	double fadeInner = 1.03;
	double fadeBlur = 0.;
	double emergePull = 1.0;
};

[[nodiscard]] Config MakeConfig() {
	auto result = Config();
	result.holeRadius = style::ConvertScale(90);
	result.sinkDepth = style::ConvertScale(30);
	result.fadeBlur = style::ConvertScale(14);
	return result;
}

// Течение к дыре: та же формула, что в вершинах сетки.
struct Flow {
	double speed = 1.;
	double strength = 0.;
	double reach = 1.;
	double ramp = 0.;
	double sink = 0.;
	double shape = 1.;
	double accel = 0.;
	double fadeHeight = 0.;

	Flow() = default;
	Flow(const Config &c, double speed)
	: speed(std::max(speed, 1.))
	, strength(c.pullStrength)
	, reach(std::max(c.pullReach * c.holeRadius, 1.))
	, ramp(c.flowRamp)
	, sink(c.sinkDepth)
	, shape(c.arcShape)
	, accel(c.flowAccel)
	, fadeHeight((std::min(c.fadeInner, c.fadeOuter) - 1.) * c.holeRadius) {
	}

	// «Высота» точки над дырой: D = rho * k(phi) - r.
	[[nodiscard]] double height(QPointF p, QPointF hole, double r) const {
		const auto dx = p.x() - hole.x();
		const auto dy = p.y() - hole.y();
		const auto rho = std::max(std::hypot(dx, dy), 0.001);
		const auto cosPhi = std::clamp(dy / rho, 0.65, 1.);
		const auto w = Smooth(r, r * 4., rho);
		return rho * std::pow(cosPhi, shape * w) - r;
	}

	[[nodiscard]] double travel(double x) const {
		return (x + reach * std::log(1. + strength * std::exp(-x / reach)))
			/ speed;
	}

	[[nodiscard]] double eased(double tau) const {
		const auto ramped = (ramp > 0.001)
			? (tau - ramp * (1. - std::exp(-tau / ramp)))
			: tau;
		return (accel > 0.001)
			? ((std::exp(accel * ramped) - 1.) / accel)
			: ramped;
	}

	[[nodiscard]] double timeToSink(double x) const {
		const auto need = travel(x) - travel(std::max(fadeHeight, -sink));
		auto lo = 0.;
		auto hi = need + ramp + 1.;
		for (auto i = 0; i != 40; ++i) {
			const auto mid = (lo + hi) / 2.;
			if (eased(mid) < need) {
				lo = mid;
			} else {
				hi = mid;
			}
		}
		return hi;
	}
};

[[nodiscard]] std::array<QPointF, 4> Corners(const QRectF &r) {
	return { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight() };
}

struct Run {
	Config config;
	QRectF island;
	double flowSpeed = 70.;
	double swallowStart = 0.;
	double swallowEnd = 0.;

	[[nodiscard]] QPointF hole() const {
		return island.center();
	}
	[[nodiscard]] double closeStart() const {
		return std::max(
			swallowEnd - std::min(0.12, config.collapseDuration * 0.3),
			swallowStart);
	}
	[[nodiscard]] double total() const {
		return closeStart()
			+ config.collapseDuration
			+ config.settleDuration
			+ 0.03;
	}
};

[[nodiscard]] Run MakeRun(
		const std::vector<Item> &items,
		QRectF island,
		Config config) {
	auto result = Run{ .config = config, .island = island };
	const auto target = island.center();
	const auto r = config.holeRadius;
	result.swallowStart = config.emergeDuration * config.swallowLead;

	const auto unit = Flow(config, 1.);
	auto farthest = 0.;
	for (const auto &item : items) {
		for (const auto &p : Corners(item.rect)) {
			farthest = std::max(farthest, unit.height(p, target, r));
		}
	}
	const auto path = unit.travel(farthest)
		- unit.travel(std::max(unit.fadeHeight, -unit.sink));
	const auto eased = unit.eased(config.swallowDuration);
	result.flowSpeed = (path > 0. && eased > 0.) ? (path / eased) : 70.;

	const auto flow = Flow(config, result.flowSpeed);
	auto end = result.swallowStart;
	for (const auto &item : items) {
		auto farHeight = 0.;
		for (const auto &p : Corners(item.rect)) {
			farHeight = std::max(farHeight, flow.height(p, target, r));
		}
		end = std::max(end, result.swallowStart + flow.timeToSink(farHeight));
	}
	result.swallowEnd = end;
	return result;
}

// Форма дыры в момент t: капсула островка -> круг.
struct State {
	QPointF center;
	QSizeF half;
	double radius = 1.;
	double presence = 0.;
	double time = 0.;
	// Плотность чёрной формы: островок без музыки появляется и тает.
	double body = 1.;
};

[[nodiscard]] State StateAt(double t, const Run &run) {
	const auto &c = run.config;
	const auto &island = run.island;
	const auto grow = Soft(Clamp01(t / c.emergeDuration));
	const auto back = OutCubic(
		Clamp01((t - run.closeStart()) / c.collapseDuration));
	const auto morph = Clamp01(grow * (1. - std::min(back, 1.)));

	// После касания островок только раздувается наружу и гаснет пружиной.
	const auto u = (t - run.closeStart() - c.collapseDuration)
		/ std::max(c.settleDuration, 0.01);
	auto bulge = 0.;
	if (u > 0. && u < 1.) {
		bulge = c.collapseBounce * std::sin(kPi * u) * std::exp(-2.2 * u);
	}
	const auto inset = style::ConvertScale(1.5);
	const auto halfWidth = Lerp(island.width() / 2. - inset, c.holeRadius, morph)
		+ island.width() / 2. * bulge * 0.5;
	const auto halfHeight = Lerp(
		island.height() / 2. - inset,
		c.holeRadius,
		morph) + island.height() / 2. * bulge * 0.5;
	const auto shift = island.height() / 2. * bulge * 0.5;
	auto result = State();
	result.center = QPointF(island.center().x(), island.center().y() + shift);
	result.half = QSizeF(std::max(halfWidth, 1.), std::max(halfHeight, 1.));
	result.radius = std::max(std::min(halfWidth, halfHeight), 1.);
	result.presence = morph;
	result.time = t;
	return result;
}

[[nodiscard]] double StadiumDistance(
		double px,
		double py,
		QPointF center,
		QSizeF half) {
	const auto r = std::min(half.width(), half.height());
	const auto qx = std::abs(px - center.x()) - (half.width() - r);
	const auto qy = std::abs(py - center.y()) - (half.height() - r);
	const auto ox = std::max(qx, 0.);
	const auto oy = std::max(qy, 0.);
	return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.) - r;
}

// Тень обоев: pow(c, 1 + 3.2a) * (1 - 0.3a), темнее и насыщеннее.
constexpr auto kShadeLevels = 128;

[[nodiscard]] const std::array<std::array<uchar, 256>, kShadeLevels> &Shade() {
	static const auto result = [] {
		auto table = std::array<std::array<uchar, 256>, kShadeLevels>();
		for (auto i = 0; i != kShadeLevels; ++i) {
			const auto a = i / double(kShadeLevels - 1);
			for (auto c = 0; c != 256; ++c) {
				const auto v = std::pow(c / 255., 1. + 3.2 * a)
					* (1. - 0.3 * a);
				table[i][c] = uchar(std::clamp(
					int(std::lround(v * 255.)),
					0,
					255));
			}
		}
		return table;
	}();
	return result;
}

// Кольцо света в полярных координатах: по горизонтали угол дуги
// theta от -1 до 1 рад, по вертикали радиус от 1.0 до 1.6 радиуса тени.
// Волокна рисуются шумом: быстро меняются по радиусу и медленно по дуге.
constexpr auto kRingTheta = 256;
constexpr auto kRingRadial = 160;
constexpr auto kRingMin = 1.0;
constexpr auto kRingMax = 1.6;

struct RingTexture {
	std::vector<float> soft;
	std::vector<float> glow;
};

[[nodiscard]] double Hash(int x, int y) {
	auto h = uint32(x) * 374761393u + uint32(y) * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return double((h ^ (h >> 16)) & 0xFFFFu) / 65535.;
}

[[nodiscard]] double Noise(double x, double y) {
	const auto ix = int(std::floor(x));
	const auto iy = int(std::floor(y));
	const auto fx = x - ix;
	const auto fy = y - iy;
	const auto sx = fx * fx * (3. - 2. * fx);
	const auto sy = fy * fy * (3. - 2. * fy);
	const auto a = Hash(ix, iy);
	const auto b = Hash(ix + 1, iy);
	const auto c = Hash(ix, iy + 1);
	const auto d = Hash(ix + 1, iy + 1);
	return Lerp(Lerp(a, b, sx), Lerp(c, d, sx), sy);
}

[[nodiscard]] const RingTexture &Ring() {
	static const auto result = [] {
		auto ring = RingTexture();
		const auto count = kRingTheta * kRingRadial;
		ring.soft.resize(count * 3);
		ring.glow.resize(count * 3);
		auto profile = std::vector<double>(kRingRadial);
		for (auto j = 0; j != kRingRadial; ++j) {
			const auto rs = kRingMin
				+ (kRingMax - kRingMin) * j / double(kRingRadial - 1);
			const auto x = (rs - 1.075) / 0.425;
			profile[j] = Smooth(-0.04, 0.015, x)
				* (std::exp(-std::max(x, 0.) / 0.06) * 1.0
					+ std::exp(-std::max(x, 0.) / 0.22) * 0.3)
				* (1. - Smooth(0.8, 1.05, x));
		}
		const auto color = [](double x, double &r, double &g, double &b) {
			// Белое у горизонта -> золото -> янтарь к краю.
			const auto t = Clamp01(x / 0.7);
			r = 1.0;
			g = Lerp(0.97, 0.8, t);
			b = Lerp(0.92, 0.52, t);
		};
		for (auto i = 0; i != kRingTheta; ++i) {
			const auto theta = -1. + 2. * i / double(kRingTheta - 1);
			for (auto j = 0; j != kRingRadial; ++j) {
				const auto rs = kRingMin
					+ (kRingMax - kRingMin) * j / double(kRingRadial - 1);
				const auto x = (rs - 1.075) / 0.425;
				const auto drift = 0.6 * std::sin(theta * 2.1 + 0.7);
				const auto fibers = 0.62 * Noise(theta * 3.5, x * 38. + drift)
					+ 0.38 * Noise(theta * 7. + 11., x * 95. + drift * 2.);
				const auto streak = 0.35 + 1.1 * fibers * fibers;
				const auto inner = std::exp(-std::max(x, 0.) / 0.05);
				const auto value = profile[j]
					* Lerp(streak, 1., inner * 0.8);
				auto r = 0., g = 0., b = 0.;
				color(x, r, g, b);
				const auto index = (i * kRingRadial + j) * 3;
				ring.soft[index] = float(std::min(value * r, 1.2));
				ring.soft[index + 1] = float(std::min(value * g, 1.2));
				ring.soft[index + 2] = float(std::min(value * b, 1.2));
			}
		}
		// Мягкий ореол: профиль, размытый по радиусу.
		for (auto j = 0; j != kRingRadial; ++j) {
			auto sum = 0.;
			auto weight = 0.;
			for (auto k = -24; k <= 24; ++k) {
				const auto jj = std::clamp(j + k, 0, kRingRadial - 1);
				const auto w = std::exp(-(k * k) / 180.);
				sum += profile[jj] * w;
				weight += w;
			}
			const auto x = (kRingMin
				+ (kRingMax - kRingMin) * j / double(kRingRadial - 1)
				- 1.075) / 0.425;
			auto r = 0., g = 0., b = 0.;
			color(std::max(x, 0.), r, g, b);
			const auto v = sum / weight;
			for (auto i = 0; i != kRingTheta; ++i) {
				const auto index = (i * kRingRadial + j) * 3;
				ring.glow[index] = float(v * r);
				ring.glow[index + 1] = float(v * g);
				ring.glow[index + 2] = float(v * b);
			}
		}
		return ring;
	}();
	return result;
}

void SampleRing(
		const std::vector<float> &data,
		double theta,
		double rs,
		float *out) {
	const auto fx = std::clamp((theta + 1.) / 2., 0., 1.) * (kRingTheta - 1);
	const auto fy = (rs - kRingMin) / (kRingMax - kRingMin) * (kRingRadial - 1);
	if (fy < 0. || fy > kRingRadial - 1) {
		out[0] = out[1] = out[2] = 0.f;
		return;
	}
	const auto x0 = std::min(int(fx), kRingTheta - 2);
	const auto y0 = std::min(int(fy), kRingRadial - 2);
	const auto ax = float(fx - x0);
	const auto ay = float(fy - y0);
	const auto at = [&](int x, int y) {
		return &data[(x * kRingRadial + y) * 3];
	};
	const auto a = at(x0, y0);
	const auto b = at(x0 + 1, y0);
	const auto c = at(x0, y0 + 1);
	const auto d = at(x0 + 1, y0 + 1);
	for (auto k = 0; k != 3; ++k) {
		const auto top = a[k] + (b[k] - a[k]) * ax;
		const auto bottom = c[k] + (d[k] - c[k]) * ax;
		out[k] = top + (bottom - top) * ay;
	}
}

// Треугольная волна (2/pi) * asin(sin(y)) без тригонометрии.
[[nodiscard]] double Triangle(double y) {
	auto t = std::fmod(y + kPi / 2., 2. * kPi);
	if (t < 0.) {
		t += 2. * kPi;
	}
	return (t < kPi) ? (t / (kPi / 2.) - 1.) : (3. - t / (kPi / 2.));
}

[[nodiscard]] int Mirror(int x, int size) {
	const auto period = 2 * size;
	auto m = x % period;
	if (m < 0) {
		m += period;
	}
	return (m >= size) ? (period - 1 - m) : m;
}

// Билинейная выборка из premultiplied ARGB32, x и y в пикселях текстуры.
struct Texel {
	float a = 0.f;
	float r = 0.f;
	float g = 0.f;
	float b = 0.f;
};

[[nodiscard]] Texel Unpack(uint32 p) {
	return {
		float(p >> 24),
		float((p >> 16) & 0xFF),
		float((p >> 8) & 0xFF),
		float(p & 0xFF),
	};
}

[[nodiscard]] Texel Bilinear(
		const uint32 *bits,
		int stride,
		int w,
		int h,
		double x,
		double y,
		bool mirror) {
	const auto fx = std::floor(x);
	const auto fy = std::floor(y);
	const auto ax = float(x - fx);
	const auto ay = float(y - fy);
	auto x0 = int(fx), y0 = int(fy), x1 = x0 + 1, y1 = y0 + 1;
	if (mirror) {
		x0 = Mirror(x0, w);
		x1 = Mirror(x1, w);
		y0 = Mirror(y0, h);
		y1 = Mirror(y1, h);
	} else {
		x0 = std::clamp(x0, 0, w - 1);
		x1 = std::clamp(x1, 0, w - 1);
		y0 = std::clamp(y0, 0, h - 1);
		y1 = std::clamp(y1, 0, h - 1);
	}
	const auto p00 = Unpack(bits[y0 * stride + x0]);
	const auto p10 = Unpack(bits[y0 * stride + x1]);
	const auto p01 = Unpack(bits[y1 * stride + x0]);
	const auto p11 = Unpack(bits[y1 * stride + x1]);
	const auto mix = [&](float a, float b, float c, float d) {
		const auto top = a + (b - a) * ax;
		const auto bottom = c + (d - c) * ax;
		return top + (bottom - top) * ay;
	};
	return {
		mix(p00.a, p10.a, p01.a, p11.a),
		mix(p00.r, p10.r, p01.r, p11.r),
		mix(p00.g, p10.g, p01.g, p11.g),
		mix(p00.b, p10.b, p01.b, p11.b),
	};
}

[[nodiscard]] uint32 Pack(float a, float r, float g, float b) {
	const auto c = [](float v) {
		return uint32(std::clamp(int(v + 0.5f), 0, 255));
	};
	return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
}

// Поверх dst кладётся premultiplied src (значения 0..255).
void BlendOver(uint32 &dst, float a, float r, float g, float b) {
	if (a <= 0.f && r <= 0.f && g <= 0.f && b <= 0.f) {
		return;
	}
	const auto d = Unpack(dst);
	const auto k = 1.f - std::clamp(a, 0.f, 255.f) / 255.f;
	dst = Pack(a + d.a * k, r + d.r * k, g + d.g * k, b + d.b * k);
}

struct Mesh {
	std::vector<QImage> levels;
	QRectF rect;
	int cols = 0;
	int rows = 0;
	int first = 0;
	bool visible = false;
};

struct Vertex {
	float x = 0.f;
	float y = 0.f;
	float u = 0.f;
	float v = 0.f;
	float alpha = 0.f;
	float lod = 0.f;
	float heat = 0.f;
};

struct VertexRef {
	int mesh = 0;
	int i = 0;
	int j = 0;
};

// Делит работу на полосы и считает их на всех ядрах.
template <typename Callback>
void Parallel(int count, Callback &&callback) {
	const auto threads = std::clamp(
		int(std::thread::hardware_concurrency()),
		1,
		8);
	const auto parts = std::min(threads, count);
	if (parts <= 1) {
		callback(0, count);
		return;
	}
	const auto chunk = (count + parts - 1) / parts;
	auto workers = std::vector<std::thread>();
	workers.reserve(parts - 1);
	for (auto i = 1; i < parts; ++i) {
		const auto from = i * chunk;
		const auto till = std::min(count, from + chunk);
		if (from >= till) {
			break;
		}
		workers.emplace_back([&callback, from, till] {
			callback(from, till);
		});
	}
	callback(0, std::min(count, chunk));
	for (auto &worker : workers) {
		worker.join();
	}
}

// Свечение вокруг кольца по расстоянию e0 (в радиусах тени).
constexpr auto kBloomSteps = 1024;
constexpr auto kBloomMax = 3.4;

[[nodiscard]] const std::array<float, kBloomSteps + 1> &BloomNear() {
	static const auto result = [] {
		auto table = std::array<float, kBloomSteps + 1>();
		for (auto i = 0; i <= kBloomSteps; ++i) {
			const auto e0 = kBloomMax * i / kBloomSteps;
			table[i] = float(std::exp(-e0 / 0.34) * 0.7
				* (1. - Smooth(2.4, 3.4, e0)));
		}
		return table;
	}();
	return result;
}

[[nodiscard]] const std::array<float, kBloomSteps + 1> &BloomFar() {
	static const auto result = [] {
		auto table = std::array<float, kBloomSteps + 1>();
		for (auto i = 0; i <= kBloomSteps; ++i) {
			const auto e0 = kBloomMax * i / kBloomSteps;
			table[i] = float(std::exp(-e0 / 1.1) * 0.12
				* (1. - Smooth(2.4, 3.4, e0)));
		}
		return table;
	}();
	return result;
}

[[nodiscard]] double OutBack(double x) {
	const auto c1 = 1.70158;
	const auto c3 = c1 + 1.;
	return 1. + c3 * std::pow(x - 1., 3.) + c1 * std::pow(x - 1., 2.);
}

struct Table {
	struct Row {
		float reach = 0.f;
		float cos = 1.f;
		float sin = 0.f;
		int shade = 0;
	};
	std::vector<Row> rows;
	double step = 0.25;
	double cutoff2 = 0.;
};

class Renderer final {
public:
	Renderer(Args &args, double ratio);

	[[nodiscard]] double total() const;
	[[nodiscard]] QSize frameSize() const;
	void render(QImage &frame, double time);

private:
	[[nodiscard]] State stateAt(double t) const;
	[[nodiscard]] Vertex vertexAt(
		const Mesh &mesh,
		double u,
		double v,
		const State &state,
		double tau) const;
	[[nodiscard]] Table makeTable(const State &state) const;
	void backdropRows(
		QImage &frame,
		const State &state,
		const Table &table,
		int from,
		int till) const;
	void meshRows(QImage &frame, int from, int till) const;
	void shade(
		QImage &frame,
		const Mesh &mesh,
		const Vertex &a,
		const Vertex &b,
		const Vertex &c,
		int from,
		int till) const;
	void overlayRows(
		QImage &frame,
		const State &state,
		int from,
		int till) const;

	double _ratio = 1.;
	Run _run;
	Flow _flow;
	double _intro = 0.;
	double _outro = 0.;
	QImage _wallpaper;
	QImage _chrome;
	int _chromeRows = 0;
	QImage _island;
	QRect _islandRect;
	std::vector<Mesh> _meshes;
	std::vector<VertexRef> _refs;
	std::vector<Vertex> _vertices;

};

Renderer::Renderer(Args &args, double ratio)
: _ratio(ratio) {
	const auto size = args.area.size() * _ratio;
	_wallpaper = args.wallpaper.convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	if (_wallpaper.size() != size) {
		_wallpaper = _wallpaper.scaled(
			size,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	}
	if (!args.chrome.isNull() && args.chromeRect.y() == 0) {
		_chrome = args.chrome.convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
		_chromeRows = std::min(_chrome.height(), size.height());
	}
	_island = args.islandImage;
	_islandRect = args.islandImageRect;

	const auto cell = double(style::ConvertScale(8));
	for (auto &item : args.items) {
		if (item.image.isNull() || item.rect.isEmpty()) {
			continue;
		}
		auto mesh = Mesh();
		mesh.rect = item.rect;
		mesh.cols = std::max(4, int(item.rect.width() / cell));
		mesh.rows = std::max(3, int(item.rect.height() / cell));
		mesh.first = int(_refs.size());
		for (auto j = 0; j <= mesh.rows; ++j) {
			for (auto i = 0; i <= mesh.cols; ++i) {
				_refs.push_back({ int(_meshes.size()), i, j });
			}
		}
		auto level = item.image.convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
		// Уровни детализации для размытия у горизонта.
		for (auto i = 0; i != 6 && !level.isNull(); ++i) {
			mesh.levels.push_back(level);
			if (level.width() < 4 || level.height() < 4) {
				break;
			}
			level = level.scaled(
				std::max(level.width() / 2, 1),
				std::max(level.height() / 2, 1),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);
		}
		_meshes.push_back(std::move(mesh));
	}
	_vertices.resize(_refs.size());
	_run = MakeRun(args.items, args.island, MakeConfig());
	_flow = Flow(_run.config, _run.flowSpeed);
	if (!args.islandStays) {
		// Без музыки островок сначала выпрыгивает пружиной,
		// а в конце сжимается и тает.
		_intro = 0.24;
		_outro = 0.2;
	}
	args.items.clear();
}

double Renderer::total() const {
	return _intro + _run.total() + _outro;
}

QSize Renderer::frameSize() const {
	return _wallpaper.size();
}

State Renderer::stateAt(double t) const {
	const auto local = t - _intro;
	if (local < 0.) {
		const auto x = Clamp01(t / _intro);
		auto result = StateAt(0., _run);
		const auto scale = 0.25 + 0.75 * OutBack(x);
		result.half = QSizeF(
			std::max(result.half.width() * scale, 1.),
			std::max(result.half.height() * scale, 1.));
		result.radius = std::min(result.half.width(), result.half.height());
		result.body = Smooth(0., 0.45, x);
		return result;
	} else if (local > _run.total()) {
		const auto x = Clamp01((local - _run.total()) / std::max(_outro, 0.01));
		auto result = StateAt(_run.total(), _run);
		const auto scale = 1. - 0.8 * x * x * x;
		result.half = QSizeF(
			std::max(result.half.width() * scale, 1.),
			std::max(result.half.height() * scale, 1.));
		result.radius = std::min(result.half.width(), result.half.height());
		result.body = 1. - Smooth(0.35, 1., x);
		return result;
	}
	return StateAt(local, _run);
}

void Renderer::render(QImage &frame, double time) {
	const auto size = frameSize();
	if (frame.size() != size) {
		frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
	}
	frame.setDevicePixelRatio(_ratio);
	const auto state = stateAt(time);
	const auto tau = (time - _intro) - _run.swallowStart;

	Parallel(int(_refs.size()), [&](int from, int till) {
		for (auto k = from; k != till; ++k) {
			const auto &ref = _refs[k];
			const auto &mesh = _meshes[ref.mesh];
			_vertices[k] = vertexAt(
				mesh,
				ref.i / double(mesh.cols),
				ref.j / double(mesh.rows),
				state,
				tau);
		}
	});
	for (auto &mesh : _meshes) {
		const auto count = (mesh.cols + 1) * (mesh.rows + 1);
		mesh.visible = false;
		for (auto k = 0; k != count; ++k) {
			if (_vertices[mesh.first + k].alpha > 0.f) {
				mesh.visible = true;
				break;
			}
		}
	}
	const auto table = makeTable(state);
	Parallel(size.height(), [&](int from, int till) {
		backdropRows(frame, state, table, from, till);
		meshRows(frame, from, till);
		overlayRows(frame, state, from, till);
	});
	if (!_island.isNull()) {
		const auto opacity = 1. - Smooth(0., 0.18, state.presence);
		if (opacity > 0.001) {
			auto p = QPainter(&frame);
			p.setOpacity(opacity);
			p.drawImage(_islandRect, _island);
		}
	}
}

Table Renderer::makeTable(const State &state) const {
	auto result = Table();
	if (state.presence < 0.002) {
		return result;
	}
	const auto &c = _run.config;
	const auto r = std::max(state.radius, 1.);
	const auto presence = state.presence;
	const auto lens = presence * c.lensStrength;
	const auto swirl = presence * c.backgroundSwirl;
	const auto darkness = presence * c.spaceDarkness;
	const auto spin = state.time * presence * c.backgroundSpin;
	const auto logical = QSizeF(frameSize()) / _ratio;
	const auto maxDist = std::hypot(logical.width(), logical.height())
		+ std::abs(state.center.y())
		+ 2.;
	const auto count = int(maxDist / result.step) + 2;
	result.rows.resize(count);
	auto cutoff = 0.;
	for (auto i = 0; i != count; ++i) {
		const auto dist = std::max(i * result.step, 0.001);
		const auto gap = std::max(dist - r, 0.);
		const auto pull = lens * r * 0.7 * std::exp(-gap / (r * 1.3));
		const auto e1 = gap / (r * 1.4);
		const auto e2 = gap / r;
		const auto twist = -(swirl * std::exp(-e1 * e1)
			+ spin * std::exp(-e2 * e2));
		const auto x = std::max(dist / r - 0.9, 0.) / c.spaceSize;
		const auto a = Clamp01(darkness * std::exp(-x * x));
		auto &row = result.rows[i];
		row.reach = float(std::max(dist - pull, r * 0.2));
		row.cos = float(std::cos(twist));
		row.sin = float(std::sin(twist));
		row.shade = std::clamp(
			int(std::lround(a * (kShadeLevels - 1))),
			0,
			kShadeLevels - 1);
		if (pull > 0.15 / _ratio
			|| std::abs(twist) * dist > 0.15 / _ratio
			|| row.shade > 0) {
			cutoff = dist;
		}
	}
	const auto rs0 = std::min(state.half.width(), state.half.height());
	const auto bloomReach = std::hypot(state.half.width(), state.half.height())
		+ rs0 * (kBloomMax + 0.1);
	cutoff = std::max(cutoff, bloomReach);
	result.cutoff2 = (cutoff + 1.) * (cutoff + 1.);
	return result;
}

void Renderer::backdropRows(
		QImage &frame,
		const State &state,
		const Table &table,
		int from,
		int till) const {
	const auto w = frame.width();
	auto dst = reinterpret_cast<uint32*>(frame.bits());
	const auto dstStride = frame.bytesPerLine() / 4;
	const auto src = reinterpret_cast<const uint32*>(_wallpaper.constBits());
	const auto srcStride = _wallpaper.bytesPerLine() / 4;
	const auto chrome = reinterpret_cast<const uint32*>(_chrome.constBits());
	const auto chromeStride = _chrome.isNull()
		? 0
		: (_chrome.bytesPerLine() / 4);
	const auto chromeWidth = std::min(_chrome.width(), w);

	const auto &shadeTable = Shade();
	const auto &bloomNear = BloomNear();
	const auto &bloomFar = BloomFar();
	const auto cx = state.center.x();
	const auto cy = state.center.y();
	const auto rs0 = std::min(state.half.width(), state.half.height());
	const auto glow = float(_run.config.glow * Smooth(0., 0.45, state.presence));
	const auto ww = _wallpaper.width();
	const auto wh = _wallpaper.height();
	const auto count = int(table.rows.size());
	for (auto y = from; y != till; ++y) {
		auto out = dst + y * dstStride;
		if (y < _chromeRows) {
			// Шапка чата остаётся на месте, дыра и сообщения идут поверх.
			memcpy(out, chrome + y * chromeStride, chromeWidth * 4);
			if (chromeWidth < w) {
				memcpy(
					out + chromeWidth,
					src + y * srcStride + chromeWidth,
					(w - chromeWidth) * 4);
			}
			continue;
		}
		const auto in = src + y * srcStride;
		if (table.rows.empty()) {
			memcpy(out, in, w * 4);
			continue;
		}
		const auto py = (y + 0.5) / _ratio;
		const auto dy = py - cy;
		for (auto x = 0; x != w; ++x) {
			const auto px = (x + 0.5) / _ratio;
			const auto dx = px - cx;
			const auto d2 = dx * dx + dy * dy;
			if (d2 > table.cutoff2) {
				out[x] = in[x];
				continue;
			}
			const auto dist = std::max(std::sqrt(d2), 0.001);
			const auto &row = table.rows[
				std::min(int(dist / table.step), count - 1)];
			const auto nx = dx / dist;
			const auto ny = dy / dist;
			const auto sx = cx + (nx * row.cos - ny * row.sin) * row.reach;
			const auto sy = cy + (nx * row.sin + ny * row.cos) * row.reach;
			const auto t = Bilinear(
				src,
				srcStride,
				ww,
				wh,
				sx * _ratio - 0.5,
				sy * _ratio - 0.5,
				true);
			const auto &shade = shadeTable[row.shade];
			auto rr = float(shade[std::clamp(int(t.r + 0.5f), 0, 255)]);
			auto gg = float(shade[std::clamp(int(t.g + 0.5f), 0, 255)]);
			auto bb = float(shade[std::clamp(int(t.b + 0.5f), 0, 255)]);
			if (glow > 0.001f) {
				// Свечение вокруг кольца прибавляется к обоям.
				const auto sd = StadiumDistance(
					px,
					py,
					state.center,
					state.half);
				const auto ratio = (sd + rs0) / rs0;
				const auto e0 = ratio - 1.;
				if (e0 >= 0. && e0 < kBloomMax) {
					const auto index = int(e0 / kBloomMax * kBloomSteps);
					const auto k = glow * float(Smooth(1., 1.06, ratio));
					const auto nearGlow = bloomNear[index] * k * 255.f;
					const auto farGlow = bloomFar[index] * k * 255.f;
					rr += nearGlow + farGlow;
					gg += nearGlow * 0.9f + farGlow * 0.85f;
					bb += nearGlow * 0.55f + farGlow * 0.5f;
				}
			}
			out[x] = Pack(255.f, rr, gg, bb);
		}
	}
}

Vertex Renderer::vertexAt(
		const Mesh &mesh,
		double u,
		double v,
		const State &state,
		double tau) const {
	const auto &c = _run.config;
	auto px = mesh.rect.x() + u * mesh.rect.width();
	auto py = mesh.rect.y() + v * mesh.rect.height();
	const auto R = std::max(state.radius, 1.);
	const auto cx = state.center.x();
	const auto cy = state.center.y();
	{
		// Пока дыра открывается, ближние сообщения уже тянутся к ней.
		const auto dx = px - cx;
		const auto dy = py - cy;
		const auto dl = std::max(std::hypot(dx, dy), 0.001);
		const auto gap = std::max(dl - R, 0.);
		const auto opening = state.presence * c.emergePull;
		const auto pull = std::min(
			opening * R * 0.7 * std::exp(-gap / (R * 1.4)),
			gap * 0.6);
		const auto mx = Lerp(dx / dl, 0., 0.6);
		const auto my = Lerp(dy / dl, 1., 0.6);
		const auto ml = std::max(std::hypot(mx, my), 0.001);
		px -= mx / ml * pull;
		py -= my / ml * pull;
	}
	auto result = Vertex();
	result.u = float(u);
	result.v = float(v);
	if (tau <= 0.) {
		result.x = float(px * _ratio);
		result.y = float(py * _ratio);
		result.alpha = 1.f;
		return result;
	}
	const auto sink = c.sinkDepth;
	const auto shape = c.arcShape;
	const auto dx = px - cx;
	const auto dy = py - cy;
	const auto rho = std::max(std::hypot(dx, dy), 0.001);
	const auto cosPhi = std::clamp(dy / rho, 0.65, 1.);
	const auto factor = [&](double value) {
		return std::pow(cosPhi, shape * Smooth(R, R * 4., value));
	};
	const auto height = rho * factor(rho) - R;

	// Новая высота: travel(D') = travel(D) - t, метод Ньютона.
	const auto &flow = _flow;
	const auto t = flow.eased(tau);
	const auto goal = flow.travel(height) - t;
	auto landed = height - flow.speed * t;
	for (auto i = 0; i != 8; ++i) {
		landed = std::clamp(landed, -sink - 40., height);
		const auto slope = 1. / (flow.speed
			* (1. + flow.strength * std::exp(-landed / flow.reach)));
		landed -= (flow.travel(landed) - goal) / slope;
	}
	landed = std::clamp(landed, -sink, height);

	auto rhoNew = (landed + R) / std::pow(cosPhi, shape);
	for (auto i = 0; i != 4; ++i) {
		rhoNew = (landed + R) / factor(rhoNew);
	}
	const auto scale = rhoNew / rho;
	const auto qx = cx + dx * scale;
	const auto qy = cy + dy * scale;

	// Над кольцом сообщение нагревается его светом и тает у горизонта.
	const auto fadeInner = std::min(c.fadeInner, c.fadeOuter - 0.05);
	const auto rr = rhoNew / R;
	const auto heat = 1. - Smooth(fadeInner, c.fadeOuter, rr);
	auto visible = Smooth(fadeInner, fadeInner + 0.18, rr);
	if (landed <= -sink + 0.5) {
		visible = 0.;
	}
	const auto maxLod = std::log2(std::max(c.fadeBlur * _ratio, 1.));
	result.x = float(qx * _ratio);
	result.y = float(qy * _ratio);
	result.alpha = float(visible);
	result.lod = float(heat * heat * maxLod);
	result.heat = float(heat);
	return result;
}

void Renderer::meshRows(QImage &frame, int from, int till) const {
	for (const auto &mesh : _meshes) {
		if (!mesh.visible) {
			continue;
		}
		const auto stride = mesh.cols + 1;
		const auto base = _vertices.data() + mesh.first;
		for (auto j = 0; j != mesh.rows; ++j) {
			for (auto i = 0; i != mesh.cols; ++i) {
				const auto &v00 = base[j * stride + i];
				const auto &v10 = base[j * stride + i + 1];
				const auto &v01 = base[(j + 1) * stride + i];
				const auto &v11 = base[(j + 1) * stride + i + 1];
				const auto top = std::min({ v00.y, v10.y, v01.y, v11.y });
				const auto bottom = std::max({ v00.y, v10.y, v01.y, v11.y });
				if (bottom < from || top >= till) {
					continue;
				}
				if (v00.alpha <= 0.f
					&& v10.alpha <= 0.f
					&& v01.alpha <= 0.f
					&& v11.alpha <= 0.f) {
					continue;
				}
				shade(frame, mesh, v00, v10, v01, from, till);
				shade(frame, mesh, v10, v11, v01, from, till);
			}
		}
	}
}

void Renderer::shade(
		QImage &frame,
		const Mesh &mesh,
		const Vertex &a,
		const Vertex &b,
		const Vertex &c,
		int from,
		int till) const {
	const auto area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
	if (std::abs(area) < 1e-4f) {
		return;
	}
	const auto w = frame.width();
	const auto left = std::max(int(std::floor(std::min({ a.x, b.x, c.x }))), 0);
	const auto right = std::min(int(std::ceil(std::max({ a.x, b.x, c.x }))), w);
	const auto top = std::max(
		int(std::floor(std::min({ a.y, b.y, c.y }))),
		from);
	const auto bottom = std::min(
		int(std::ceil(std::max({ a.y, b.y, c.y }))),
		till);
	if (left >= right || top >= bottom) {
		return;
	}
	// Правило верхнего-левого ребра: общий край двух треугольников
	// закрашивается один раз, иначе на прозрачном видна сетка.
	const auto sign = (area > 0.f) ? 1.f : -1.f;
	const auto topLeft = [&](const Vertex &p, const Vertex &q) {
		const auto ex = (q.x - p.x) * sign;
		const auto ey = (q.y - p.y) * sign;
		return (ey == 0.f && ex < 0.f) || (ey > 0.f);
	};
	const auto bias0 = topLeft(b, c) ? 0.f : -1e-6f;
	const auto bias1 = topLeft(c, a) ? 0.f : -1e-6f;
	const auto bias2 = topLeft(a, b) ? 0.f : -1e-6f;
	const auto inv = 1.f / std::abs(area);

	// Рёбра считаются приращениями: шаг по x меняет их на константу.
	const auto e0x = -sign * (c.y - b.y);
	const auto e1x = -sign * (a.y - c.y);
	const auto e2x = -sign * (b.y - a.y);
	const auto edge = [&](const Vertex &p, const Vertex &q, float x, float y) {
		return sign * ((q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x));
	};

	const auto &levels = mesh.levels;
	const auto maxLevel = int(levels.size()) - 1;
	auto dst = reinterpret_cast<uint32*>(frame.bits());
	const auto stride = frame.bytesPerLine() / 4;
	const auto sample = [&](int level, float u, float v) {
		const auto &image = levels[level];
		const auto iw = image.width();
		const auto ih = image.height();
		return Bilinear(
			reinterpret_cast<const uint32*>(image.constBits()),
			image.bytesPerLine() / 4,
			iw,
			ih,
			u * iw - 0.5,
			v * ih - 0.5,
			false);
	};
	for (auto y = top; y != bottom; ++y) {
		const auto py = y + 0.5f;
		auto row = dst + y * stride;
		const auto px0 = left + 0.5f;
		auto w0 = edge(b, c, px0, py);
		auto w1 = edge(c, a, px0, py);
		auto w2 = edge(a, b, px0, py);
		for (auto x = left; x != right; ++x, w0 += e0x, w1 += e1x, w2 += e2x) {
			if (w0 + bias0 < 0.f || w1 + bias1 < 0.f || w2 + bias2 < 0.f) {
				continue;
			}
			const auto l0 = w0 * inv;
			const auto l1 = w1 * inv;
			const auto l2 = w2 * inv;
			const auto alpha = a.alpha * l0 + b.alpha * l1 + c.alpha * l2;
			if (alpha <= 0.002f) {
				continue;
			}
			const auto u = a.u * l0 + b.u * l1 + c.u * l2;
			const auto v = a.v * l0 + b.v * l1 + c.v * l2;
			const auto lod = std::clamp(
				a.lod * l0 + b.lod * l1 + c.lod * l2,
				0.f,
				float(maxLevel));
			auto t = Texel();
			if (lod < 0.02f || maxLevel == 0) {
				t = sample(0, u, v);
			} else {
				const auto low = std::min(int(lod), maxLevel - 1);
				const auto k = lod - low;
				const auto t0 = sample(low, u, v);
				const auto t1 = sample(low + 1, u, v);
				t = {
					t0.a + (t1.a - t0.a) * k,
					t0.r + (t1.r - t0.r) * k,
					t0.g + (t1.g - t0.g) * k,
					t0.b + (t1.b - t0.b) * k,
				};
			}
			if (t.a <= 0.f) {
				continue;
			}
			const auto heat = a.heat * l0 + b.heat * l1 + c.heat * l2;
			if (heat > 0.001f) {
				// Цвет уходит в тёплый свет кольца: белый -> золото.
				const auto h2 = heat * heat;
				const auto gg = 0.84f + (0.97f - 0.84f) * h2;
				const auto gb = 0.55f + (0.9f - 0.55f) * h2;
				const auto x2 = std::clamp(heat / 0.85f, 0.f, 1.f);
				const auto s = x2 * x2 * (3.f - 2.f * x2) * 0.85f;
				const auto add = heat * 0.35f;
				t.r = t.r * (1.f - s) + t.a * (s + add);
				t.g = t.g * (1.f - s) + gg * t.a * (s + add);
				t.b = t.b * (1.f - s) + gb * t.a * (s + add);
			}
			BlendOver(
				row[x],
				t.a * alpha,
				t.r * alpha,
				t.g * alpha,
				t.b * alpha);
		}
	}
}

void Renderer::overlayRows(
		QImage &frame,
		const State &state,
		int from,
		int till) const {
	if (state.body <= 0.001) {
		return;
	}
	const auto &c = _run.config;
	const auto r = std::max(state.radius, 1.);
	const auto ringWidth = 0.425;
	const auto ringScaled = ringWidth / std::max(c.ringSqueeze, 0.1);
	const auto extent = r * (ringScaled + 0.05);
	const auto vis = Smooth(0., 0.45, state.presence);
	const auto left = std::max(
		int(std::floor((state.center.x() - state.half.width() - extent)
			* _ratio)),
		0);
	const auto right = std::min(
		int(std::ceil((state.center.x() + state.half.width() + extent)
			* _ratio)),
		frame.width());
	const auto top = std::max(
		int(std::floor((state.center.y() - state.half.height() - extent)
			* _ratio)),
		from);
	const auto bottom = std::min(
		int(std::ceil((state.center.y() + state.half.height() + extent)
			* _ratio)),
		till);
	const auto &ring = Ring();
	const auto aa = 0.6;
	const auto bodyAlpha = state.body;
	auto dst = reinterpret_cast<uint32*>(frame.bits());
	const auto stride = frame.bytesPerLine() / 4;
	const auto maxQs = 1. + ringScaled + 0.05;
	for (auto y = top; y < bottom; ++y) {
		const auto py = (y + 0.5) / _ratio;
		auto row = dst + y * stride;
		for (auto x = left; x < right; ++x) {
			const auto px = (x + 0.5) / _ratio;
			const auto sd = StadiumDistance(px, py, state.center, state.half);
			const auto body = (1. - Smooth(-aa, aa, sd)) * bodyAlpha;
			if (body >= 0.999) {
				row[x] = 0xFF000000u;
				continue;
			}
			const auto qs = 1. + sd / r;
			if (vis < 0.002 || qs > maxQs) {
				if (body > 0.001) {
					BlendOver(row[x], float(body * 255.), 0.f, 0.f, 0.f);
				}
				continue;
			}
			const auto qx = (px - state.center.x()) / r;
			const auto qy = (py - state.center.y()) / r;
			const auto pd = (qs - 1.012) / 0.0065;
			const auto photon = (std::abs(pd) < 4.) ? std::exp(-pd * pd) : 0.;
			// Кольцо: чистая верхняя дуга текстуры вокруг всей дыры, волокна
			// у горизонта крутятся быстрее, как орбиты.
			const auto phi = std::atan2(qx, qy);
			const auto proximity = std::exp(
				-std::max(qs - 1., 0.) / std::max(c.innerWidth, 0.01));
			const auto spin = state.time
				* c.ringSpin
				* c.ringSpeed
				* (0.6 + c.innerBoost * proximity);
			const auto theta = Triangle(phi + spin);
			const auto xr = std::max(qs - 1., 0.);
			const auto rs = 1.075
				+ ringWidth * std::pow(xr / ringScaled, c.ringGamma);
			float soft[3];
			float halo[3];
			SampleRing(ring.soft, theta, rs, soft);
			SampleRing(ring.glow, theta, rs, halo);
			auto cr = soft[0] + halo[0] * 0.22f;
			auto cg = soft[1] + halo[1] * 0.22f;
			auto cb = soft[2] + halo[2] * 0.22f;
			const auto l = std::max({ cr, cg, cb });
			const auto brightness = float(c.ringBrightness);
			cr = (cr + (l * 1.f - cr) * 0.6f) * brightness;
			cg = (cg + (l * 0.95f - cg) * 0.6f) * brightness;
			cb = (cb + (l * 0.86f - cb) * 0.6f) * brightness;
			auto a = std::clamp(std::max({ cr, cg, cb }) * 0.9f, 0.f, 1.f);
			const auto keep = float(Smooth(0.995, 1.004, qs));
			cr *= keep;
			cg *= keep;
			cb *= keep;
			a *= keep;
			cr += float(photon * 0.95);
			cg += float(photon * 0.95);
			cb += float(photon * 0.95);
			a = std::max(a, float(photon));
			const auto v = float(vis);
			cr = std::min(cr, 1.f) * v;
			cg = std::min(cg, 1.f) * v;
			cb = std::min(cb, 1.f) * v;
			a *= v;
			const auto open = float(1. - body);
			BlendOver(
				row[x],
				(a * open + float(body)) * 255.f,
				cr * open * 255.f,
				cg * open * 255.f,
				cb * open * 255.f);
		}
	}
}

class Effect final : public Ui::RpWidget, public base::has_weak_ptr {
public:
	Effect(Args &&args);
	~Effect();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void tick();
	void keepOnTop();
	void kick();
	void rendered(QImage frame);
	void finish();

	const double _ratio = 1.;
	std::shared_ptr<Renderer> _renderer;
	QImage _front;
	QImage _back;
	double _time = 0.;
	double _fade = 0.;
	crl::time _last = 0;
	bool _busy = false;
	bool _finishing = false;
	bool _finished = false;
	Ui::Animations::Basic _animation;

};

Effect::Effect(Args &&args)
: RpWidget(args.parent)
, _ratio(style::DevicePixelRatio())
, _animation([=] { tick(); }) {
	++Active;
	setAttribute(Qt::WA_TransparentForMouseEvents);
	// Кадр непрозрачный: чат под слоем не перерисовывается каждый кадр.
	setAttribute(Qt::WA_OpaquePaintEvent);
	setGeometry(args.area);
	const auto items = int(args.items.size());
	_renderer = std::make_shared<Renderer>(args, _ratio);
	_renderer->render(_front, 0.);
	_back = QImage(_front.size(), QImage::Format_ARGB32_Premultiplied);
	LOG(("BlackHole: start, total %1, items %2"
		).arg(_renderer->total()).arg(items));


	show();
	raise();
	_last = crl::now();
	_animation.start();
}

Effect::~Effect() {
	--Active;
}

void Effect::finish() {
	if (_finished) {
		return;
	}
	_finished = true;
	LOG(("BlackHole: finished at %1 of %2, fade %3"
		).arg(_time).arg(_renderer->total()).arg(_fade));
	_animation.stop();
	hide();
	deleteLater();
}

// После очистки чат перестраивается и может поднять соседей наверх.
void Effect::keepOnTop() {
	if (const auto parent = parentWidget()) {
		const auto &siblings = parent->children();
		if (!siblings.isEmpty() && siblings.back() != this) {
			raise();
		}
	}
}

void Effect::tick() {
	keepOnTop();
	const auto now = crl::now();
	// Шаг ограничен: после просевшего кадра анимация идёт дальше, а не прыгает.
	const auto step = std::min((now - _last) / 1000., kMaxStep);
	_last = now;
	if (_finishing) {
		_fade += step;
		if (_fade >= kFadeOut) {
			finish();
		} else {
			update();
		}
		return;
	}
	_time = std::min(_time + step, _renderer->total());
	if (!_busy) {
		kick();
	}
}

// Кадр считается в фоне, главный поток только показывает готовый.
void Effect::kick() {
	_busy = true;
	auto frame = base::take(_back);
	if (frame.size() != _front.size()) {
		frame = QImage(_front.size(), QImage::Format_ARGB32_Premultiplied);
	}
	crl::async([
			renderer = _renderer,
			frame = std::move(frame),
			time = _time,
			weak = base::make_weak(this)]() mutable {
		renderer->render(frame, time);
		crl::on_main(weak, [=]() mutable {
			weak->rendered(std::move(frame));
		});
	});
}

void Effect::rendered(QImage frame) {
	_busy = false;
	_back = std::move(_front);
	_front = std::move(frame);
	update();
	if (_time >= _renderer->total() && !_finishing) {
		// Под эффектом уже пустой чат: слой просто растворяется.
		_finishing = true;
		setAttribute(Qt::WA_OpaquePaintEvent, false);
	}
}

void Effect::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (_finishing) {
		p.setOpacity(1. - Clamp01(_fade / kFadeOut));
	}
	p.drawImage(0, 0, _front);
}

} // namespace

std::vector<Item> Split(const QImage &shot, QPointF origin) {
	auto result = std::vector<Item>();
	if (shot.isNull()) {
		return result;
	}
	const auto image = shot.convertToFormat(
		QImage::Format_ARGB32_Premultiplied);
	const auto ratio = shot.devicePixelRatio();
	const auto w = image.width();
	const auto h = image.height();
	const auto bits = reinterpret_cast<const uint32*>(image.constBits());
	const auto stride = image.bytesPerLine() / 4;
	const auto filled = [&](int y) {
		const auto row = bits + y * stride;
		for (auto x = 0; x != w; ++x) {
			if ((row[x] >> 24) > 8) {
				return true;
			}
		}
		return false;
	};
	const auto pad = 2;
	auto y = 0;
	while (y < h) {
		while (y < h && !filled(y)) {
			++y;
		}
		const auto from = y;
		while (y < h && filled(y)) {
			++y;
		}
		const auto till = y;
		if (till - from < 2) {
			continue;
		}
		auto minX = w;
		auto maxX = -1;
		for (auto yy = from; yy != till; ++yy) {
			const auto row = bits + yy * stride;
			for (auto x = 0; x != w; ++x) {
				if ((row[x] >> 24) > 8) {
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
				}
			}
		}
		if (maxX < minX) {
			continue;
		}
		const auto rect = QRect(
			minX - pad,
			from - pad,
			maxX - minX + 1 + 2 * pad,
			till - from + 2 * pad);
		auto piece = QImage(rect.size(), QImage::Format_ARGB32_Premultiplied);
		piece.fill(Qt::transparent);
		{
			auto p = QPainter(&piece);
			p.setCompositionMode(QPainter::CompositionMode_Source);
			p.drawImage(QPoint(), image, rect);
		}
		piece.setDevicePixelRatio(ratio);
		result.push_back({
			.image = std::move(piece),
			.rect = QRectF(
				origin.x() + rect.x() / ratio,
				origin.y() + rect.y() / ratio,
				rect.width() / ratio,
				rect.height() / ratio),
		});
	}
	return result;
}

bool Supported() {
	return !PowerSaving::On(PowerSaving::kAnimations)
		&& !PowerSaving::On(PowerSaving::kChatEffects);
}

bool Running() {
	return Active > 0;
}

void Start(Args &&args) {
	if (args.area.isEmpty() || args.wallpaper.isNull()) {
		return;
	}
	new Effect(std::move(args));
}

bool ClearHistory(not_null<PeerData*> peer, Fn<void()> clear) {
	if (!Supported() || Running()) {
		return false;
	}
	const auto window = Core::App().windowForShowingHistory(peer);
	const auto controller = window ? window->sessionController() : nullptr;
	if (!controller) {
		return false;
	}
	const auto history = controller->content()->mokaHistoryWidget();
	return history && history->mokaClearWithBlackHole(peer, std::move(clear));
}

} // namespace Mokagram::BlackHole

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/premium_3d_mesh.h"

#include <QtCore/QFile>
#include <QtCore/QDataStream>
#include <QtGui/QImage>

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include "ui/rhi/rhi_shader.h"
#include <rhi/qrhi.h>
#endif // Qt >= 6.7

namespace Ui::Premium {
namespace {

[[nodiscard]] std::vector<float> ReadFloats(QDataStream &stream, int count) {
	auto result = std::vector<float>(count);
	for (auto i = 0; i != count; ++i) {
		stream >> result[i];
	}
	return result;
}

} // namespace

Object3dMesh LoadObject3dMesh(
		const QString &path,
		float scale,
		bool withUv) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	auto stream = QDataStream(&file);
	stream.setByteOrder(QDataStream::BigEndian);
	stream.setFloatingPointPrecision(QDataStream::SinglePrecision);

	const auto readCount = [&]() -> int {
		auto value = qint32(0);
		stream >> value;
		return (stream.status() == QDataStream::Ok && value >= 0)
			? value
			: -1;
	};

	const auto positionFloats = readCount();
	if (positionFloats < 0) {
		return {};
	}
	const auto positions = ReadFloats(stream, positionFloats);

	const auto uvFloats = readCount();
	if (uvFloats < 0) {
		return {};
	}
	const auto uvs = ReadFloats(stream, uvFloats);

	const auto normalFloats = readCount();
	if (normalFloats < 0) {
		return {};
	}
	const auto normals = ReadFloats(stream, normalFloats);

	const auto faceCount = readCount();
	if (faceCount < 0 || stream.status() != QDataStream::Ok) {
		return {};
	}

	const auto floatsPerVertex = withUv ? 8 : 6;
	auto result = Object3dMesh();
	result.vertices.reserve(faceCount * floatsPerVertex);
	for (auto i = 0; i != faceCount; ++i) {
		auto positionIndex = qint32(0);
		auto uvIndex = qint32(0);
		auto normalIndex = qint32(0);
		stream >> positionIndex >> uvIndex >> normalIndex;
		if (stream.status() != QDataStream::Ok) {
			return {};
		}

		const auto position = qint64(positionIndex) * 3;
		const auto normal = qint64(normalIndex) * 3;
		if (position < 0
			|| position + 2 >= positionFloats
			|| normal < 0
			|| normal + 2 >= normalFloats) {
			return {};
		}
		result.vertices.push_back(positions[position + 0] * scale);
		result.vertices.push_back(positions[position + 1] * scale);
		result.vertices.push_back(positions[position + 2] * scale);

		result.vertices.push_back(normals[normal + 0]);
		result.vertices.push_back(normals[normal + 1]);
		result.vertices.push_back(normals[normal + 2]);

		if (withUv) {
			const auto uv = qint64(uvIndex) * 2;
			const auto valid = (uv >= 0) && (uv + 1 < uvFloats);
			result.vertices.push_back(valid ? uvs[uv] : 0.f);
			result.vertices.push_back(valid ? (1.f - uvs[uv + 1]) : 0.f);
		}
	}
	result.vertexCount = faceCount;
	return result;
}

Object3dModel LoadObject3dModel(
		const std::vector<QString> &paths,
		float scale,
		bool withUv) {
	auto result = Object3dModel();
	result.shells.reserve(paths.size());
	for (const auto &path : paths) {
		result.shells.push_back(LoadObject3dMesh(path, scale, withUv));
	}
	return result;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
QShader LoadObject3dShader(const QString &name) {
	return Rhi::ShaderFromFile(u":/shaders/"_q + name + u".qsb"_q);
}
#endif // Qt >= 6.7

QImage LoadObject3dFlecksTexture() {
	return QImage(
		u":/gui/art/premium/flecks.png"_q
	).convertToFormat(QImage::Format_RGBA8888);
}

} // namespace Ui::Premium

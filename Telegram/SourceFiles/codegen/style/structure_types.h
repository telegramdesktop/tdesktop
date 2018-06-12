/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>
#include <vector>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtMath>

namespace codegen {
namespace style {
namespace structure {

// List of names, like overview.document.bg
using FullName = QStringList;
inline std::string logFullName(const FullName &name) {
	return name.join('.').toStdString();
}

struct Variable;
class Value;

enum class TypeTag {
	Invalid,
	Int,
	Double,
	Pixels,
	String,
	Color,
	Point,
	Size,
	Align,
	Margins,
	Font,
	Icon,
	Struct,
};

struct Type {
	TypeTag tag;
	FullName name; // only for type == ClassType::Struct

	explicit operator bool() const {
		return (tag != TypeTag::Invalid);
	}
};
inline bool operator==(const Type &a, const Type &b) {
	return (a.tag == b.tag) && (a.name == b.name);
}
inline bool operator!=(const Type &a, const Type &b) {
	return !(a == b);
}

namespace data {

inline int pxAdjust(int value, int scale) {
	if (value < 0) {
		return -pxAdjust(-value, scale);
	}
	return qFloor((value * scale / 4.) + 0.1);
}

struct point {
	int x, y;
};

struct size {
	int width, height;
};

struct color {
	uchar red, green, blue, alpha;
	QString fallback;
};

struct margins {
	int left, top, right, bottom;
};

struct font {
	enum Flag {
		Bold      = 0x01,
		Italic    = 0x02,
		Underline = 0x04,
	};
	std::string family;
	int size;
	int flags;
};

struct monoicon;
struct icon {
	std::vector<monoicon> parts;
};

struct field; // defined after Variable is defined
using fields = QList<field>;

} // namespace data

class Value {
public:
	Value();
	Value(data::point value);
	Value(data::size value);
	Value(data::color value);
	Value(data::margins value);
	Value(data::font value);
	Value(data::icon value);
	Value(const FullName &type, data::fields value);

	// Can be only double.
	Value(TypeTag type, double value);

	// Can be int / pixels.
	Value(TypeTag type, int value);

	// Can be string / align.
	Value(TypeTag type, std::string value);

	// Default constructed value (uninitialized).
	Value(Type type, Qt::Initialization);

	Type type() const { return type_; }
	int Int() const { return data_->Int(); }
	double Double() const { return data_->Double(); }
	std::string String() const { return data_->String(); }
	data::point Point() const { return data_->Point(); }
	data::size Size() const { return data_->Size(); };
	data::color Color() const { return data_->Color(); };
	data::margins Margins() const { return data_->Margins(); };
	data::font Font() const { return data_->Font(); };
	data::icon Icon() const { return data_->Icon(); };
	const data::fields *Fields() const { return data_->Fields(); };
	data::fields *Fields() { return data_->Fields(); };

	explicit operator bool() const {
		return type_.tag != TypeTag::Invalid;
	}

	Value makeCopy(const FullName &copyOf) const {
		Value result(*this);
		result.copyOf_ = copyOf;
		return result;
	}

	const FullName &copyOf() const {
		return copyOf_;
	}

private:
	class DataBase {
	public:
		virtual int Int() const { return 0; }
		virtual double Double() const { return 0.; }
		virtual std::string String() const { return std::string(); }
		virtual data::point Point() const { return {}; };
		virtual data::size Size() const { return {}; };
		virtual data::color Color() const { return {}; };
		virtual data::margins Margins() const { return {}; };
		virtual data::font Font() const { return {}; };
		virtual data::icon Icon() const { return {}; };
		virtual const data::fields *Fields() const { return nullptr; };
		virtual data::fields *Fields() { return nullptr; };
		virtual ~DataBase() {
		}
	};
	struct DataTypes;

	Value(TypeTag type, std::shared_ptr<DataBase> &&data);

	Type type_;
	std::shared_ptr<DataBase> data_;

	FullName copyOf_; // for copies of existing named values

};

struct Variable {
	FullName name;
	Value value;
	QString description;

	explicit operator bool() const {
		return !name.isEmpty();
	}
};

namespace data {
struct field {
	enum class Status {
		Uninitialized,
		Implicit,
		Explicit
	};
	Variable variable;
	Status status;
};

struct monoicon {
	QString filename;
	Value color;
	Value offset;

	explicit operator bool() const {
		return !filename.isEmpty();
	}
};
} // namespace data

struct StructField {
	FullName name;
	Type type;

	explicit operator bool() const {
		return !name.isEmpty();
	}
};

struct Struct {
	FullName name;
	QList<StructField> fields;

	explicit operator bool() const {
		return !name.isEmpty();
	}
};

} // namespace structure
} // namespace style
} // namespace codegen

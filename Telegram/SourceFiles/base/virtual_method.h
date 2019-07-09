/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace base {

template <typename Object, typename ParentObject = void>
class virtual_object;

template <typename ConcreteMethod, typename ReturnType, typename ...Args>
class virtual_method;

template <typename ConcreteMethod, typename BaseMethod>
class virtual_override;

namespace virtual_methods {

struct child_entry;
using is_parent_check = bool(*)(const child_entry &possible_parent);
struct child_entry {
	is_parent_check check_is_parent;
	int *table_index;
};
using child_entries = std::vector<child_entry>;

// Recursive method to find if some class is a child of some other class.
template <typename ConcreteObject>
struct is_parent {
	static inline bool check(const child_entry &possible_parent) {
		// Generate a good error message if ConcreteObject is not a child of virtual_object<>.
		using all_objects_must_derive_virtual_object = typename ConcreteObject::virtual_object_parent;
		using ConcreteObjectParent = all_objects_must_derive_virtual_object;
		return (possible_parent.check_is_parent == &is_parent<ConcreteObject>::check)
			|| is_parent<ConcreteObjectParent>::check(possible_parent);
	}
};

template <>
struct is_parent<void> {
	static inline bool check(const child_entry &possible_parent) {
		return (possible_parent.check_is_parent == &is_parent<void>::check);
	}
};

// Just force the compiler not to optimize away the object that "enforce" points at.
inline void dont_optimize_away(void *enforce) {
	static volatile void *result = nullptr;
	if (result) {
		result = enforce;
	}
}

template <typename Type, Type Value>
struct dont_optimize_away_struct {
};

inline bool first_dispatch_fired(bool did_fire = false) {
	static bool fired = false;
	if (did_fire) {
		fired = true;
	}
	return fired;
}

template <typename Object, void (*Creator)(const child_entry &)>
class object_registrator {
public:
	inline object_registrator() {
		Assert(!first_dispatch_fired());
		Creator(child_entry {
			&is_parent<Object>::check,
			&_index,
		});
	}
	static inline int &Index() {
		return _index;
	}

private:
	static int _index;

};

template <typename Object, void (*Creator)(const child_entry &)>
int object_registrator<Object, Creator>::_index = -1;

class object_base {
protected:
	virtual ~object_base() = default;

};

template <typename ...ConcreteArgs>
struct multi_index_collector;
template <int M, typename ...ConcreteArgs>
struct override_key_collector_helper;
template <typename Call, typename ...Args>
struct table_fill_entry_helper;
template <typename ...Args>
struct table_count_size;

} // namespace virtual_methods

// This should be a base class for every child object in your hierarchy.
// It registers this child in the root virtual_objects classes list.
// Also it holds its own index in the classes list that is used for fast
// invoking of methods from the virtual tables in different virtual_methods.
template <typename Object, typename ParentObject>
class virtual_object : public ParentObject {
protected:
	virtual ~virtual_object() {
		virtual_methods::dont_optimize_away(&_virtual_object_registrator);
	}

private:
	using virtual_object_parent = ParentObject;

	friend struct virtual_methods::is_parent<Object>;
	template <typename ...Args>
	friend struct virtual_methods::multi_index_collector;
	template <int M, typename ...ConcreteArgs>
	friend struct virtual_methods::override_key_collector_helper;
	template <typename OtherObject, typename OtherParentObject>
	friend class virtual_object;
	template <typename BaseMethod, typename ReturnType, typename ...Args>
	friend class virtual_method;

	static inline void virtual_object_register_child(const virtual_methods::child_entry &entry) {
		return ParentObject::virtual_object_register_child(entry);
	}

	using virtual_object_registrator = virtual_methods::object_registrator<Object, &virtual_object::virtual_object_register_child>;
	static virtual_object_registrator _virtual_object_registrator;
	using virtual_object_dont_optimize_away_registrator = virtual_methods::dont_optimize_away_struct<virtual_object_registrator*, &_virtual_object_registrator>;

	static inline int &virtual_object_child_index_static() {
		return virtual_object_registrator::Index();
	}
	int &virtual_object_child_index() override {
		return virtual_object_child_index_static();
	}

};

template <typename Object, typename ParentObject>
typename virtual_object<Object, ParentObject>::virtual_object_registrator virtual_object<Object, ParentObject>::_virtual_object_registrator = {};

// This should be a base class for the root of the whole hierarchy.
// It holds the table of all child classes in a list.
// This list is used by virtual_methods to generate virtual table.
template <typename Object>
class virtual_object<Object, void> : public virtual_methods::object_base {
protected:
	virtual ~virtual_object() {
		virtual_methods::dont_optimize_away(&_virtual_object_registrator);
	}

private:
	using virtual_object_parent = void;

	friend struct virtual_methods::is_parent<Object>;
	template <typename ...Args>
	friend struct virtual_methods::table_count_size;
	template <typename ...Args>
	friend struct virtual_methods::multi_index_collector;
	template <int M, typename ...ConcreteArgs>
	friend struct virtual_methods::override_key_collector_helper;
	template <typename Call, typename ...Args>
	friend struct virtual_methods::table_fill_entry_helper;
	template <typename OtherObject, typename OtherParentObject>
	friend class virtual_object;
	template <typename BaseMethod, typename ReturnType, typename ...Args>
	friend class virtual_method;

	static inline virtual_methods::child_entries &virtual_object_get_child_entries() {
		static virtual_methods::child_entries entries;
		return entries;
	}

	// Registers a new child class.
	// After that on the next call to virtual_method::virtual_method_prepare_table() will
	// generate a new virtual table for that virtual method.
	static inline void virtual_object_register_child(const virtual_methods::child_entry &entry) {
		auto &entries = virtual_object_get_child_entries();
		for (auto i = entries.begin(), e = entries.end(); i != e; ++i) {
			if (entry.check_is_parent(*i)) {
				*entry.table_index = (i - entries.begin());
				i = entries.insert(i, entry);
				for (++i, e = entries.end(); i != e; ++i) {
					++*(i->table_index);
				}
				return;
			}
		}
		*entry.table_index = entries.size();
		entries.push_back(entry);
	}

	using virtual_object_registrator = virtual_methods::object_registrator<Object, &virtual_object::virtual_object_register_child>;
	static virtual_object_registrator _virtual_object_registrator;
	using virtual_object_dont_optimize_away_registrator = virtual_methods::dont_optimize_away_struct<virtual_object_registrator*, &_virtual_object_registrator>;

	static inline int &virtual_object_child_index_static() {
		return virtual_object_registrator::Index();
	}
	virtual int &virtual_object_child_index() {
		return virtual_object_child_index_static();
	}

};

template <typename Object>
typename virtual_object<Object, void>::virtual_object_registrator virtual_object<Object, void>::_virtual_object_registrator = {};

namespace virtual_methods {

template <typename Arg>
struct is_virtual_argument : public std::integral_constant<bool,
	base::type_traits<Arg>::is_pointer::value
	? std::is_base_of<object_base, typename base::type_traits<Arg>::pointed_type>::value
	: false> {
};

template <int N, int Instance>
class multi_int_wrap {
public:
	inline multi_int_wrap(int *indices) : _indices(indices) {
	}
	inline multi_int_wrap<N - 1, Instance> subindex() const {
		static_assert(N > 0, "Wrong multi_int_wrap created!");
		return multi_int_wrap<N - 1, Instance>(_indices + 1);
	}
	inline int &current() const {
		return *_indices;
	}

private:
	int *_indices;

};

template <int Instance>
class multi_int_wrap<0, Instance> {
public:
	inline multi_int_wrap(int *indices) {
	}
	inline int current() const {
		return 1;
	}

};

template <int N>
using multi_index_wrap = multi_int_wrap<N, 0>;
template <int N>
using multi_size_wrap = multi_int_wrap<N, 1>;

template <typename ConcreteArg, typename ...ConcreteArgs>
struct multi_index_collector<ConcreteArg, ConcreteArgs...> {
	static constexpr int N = sizeof...(ConcreteArgs) + 1;
	static inline void call(multi_index_wrap<N> indices, ConcreteArg arg, ConcreteArgs... args) {
		indices.current() = computeIndex(is_virtual_argument<ConcreteArg>(), arg);
		multi_index_collector<ConcreteArgs...>::call(indices.subindex(), args...);
	}

	static inline int computeIndex(std::integral_constant<bool, false>, ConcreteArg arg) {
		return 0;
	}
	static inline int computeIndex(std::integral_constant<bool, true>, ConcreteArg arg) {
		return arg->virtual_object_child_index();
	}

};

template <>
struct multi_index_collector<> {
	static inline void call(multi_index_wrap<0> indices) {
	}
};

template <int N>
class override_key;

template <int N, int Instance>
class multi_int {
public:
	inline multi_int_wrap<N, Instance> data_wrap() {
		return multi_int_wrap<N, Instance>(_indices);
	}

	template <typename ...ConcreteArgs>
	static inline multi_int<N, Instance> collect(ConcreteArgs... args) {
		multi_int<N, Instance> result;
		multi_index_collector<ConcreteArgs...>::call(result.data_wrap(), args...);
		return result;
	}

	inline void reset() {
		memset(_indices, 0, sizeof(_indices));
	}

	inline int value(int index) const {
		return _indices[index];
	}

	inline void copy(multi_int_wrap<N, Instance> other) {
		memcpy(_indices, &other.current(), sizeof(_indices));
	}

private:
	int _indices[N] = { 0 };
	friend class override_key<N>;

};

template <int N>
using multi_index = multi_int<N, 0>;
template <int N>
using multi_size = multi_int<N, 1>;

template <typename Call, int N>
class table_data_wrap {
public:
	inline table_data_wrap(Call *data, multi_size_wrap<N> size) : _data(data), _size(size) {
	}
	inline table_data_wrap<Call, N - 1> operator[](int index) const {
		return table_data_wrap<Call, N - 1>(_data + index * _size.subindex().current(), _size.subindex());
	}
	inline Call &operator[](multi_index_wrap<N> index) const {
		return (*this)[index.current()][index.subindex()];
	}
	inline int size() const {
		return count_size(std::integral_constant<int,N>());
	}

private:
	template <int M>
	inline int count_size(std::integral_constant<int,M>) const {
		return _size.current() / _size.subindex().current();
	}
	inline int count_size(std::integral_constant<int,1>) const {
		return _size.current();
	}

	Call *_data;
	multi_size_wrap<N> _size;

};

template <typename Call>
class table_data_wrap<Call, 0> {
public:
	inline table_data_wrap(Call *data, multi_size_wrap<0> size) : _data(data) {
	}
	inline Call &operator[](multi_index_wrap<0> index) const {
		return *_data;
	}

private:
	Call *_data;

};

template <typename Call, int N>
class table_data_wrap;

template <typename Arg, typename ...Args>
struct table_count_size<Arg, Args...> {
	static constexpr int N = sizeof...(Args) + 1;
	static inline void call(multi_size_wrap<N> index) {
		auto subindex = index.subindex();
		table_count_size<Args...>::call(subindex);
		index.current() = count(is_virtual_argument<Arg>()) * subindex.current();
	}

	static inline int count(std::integral_constant<bool, false>) {
		return 1;
	}
	static inline int count(std::integral_constant<bool, true>) {
		return base::type_traits<Arg>::pointed_type::virtual_object_get_child_entries().size();
	}

};

template <>
struct table_count_size<> {
	static inline void call(multi_size_wrap<0> index) {
	}
};

template <typename Call, int N>
class table_data {
public:
	inline table_data_wrap<Call, N> data_wrap() {
		return table_data_wrap<Call, N>(_data.data(), _size.data_wrap());
	}

	inline Call &operator[](multi_index<N> index) {
		int flat_index = 0;
		for (int i = 0; i != N - 1; ++i) {
			flat_index += _size.value(i + 1) * index.value(i);
		}
		flat_index += index.value(N - 1);
		return _data[flat_index];
	}

	template <typename ...Args>
	inline bool changed() {
		if (!_data.empty()) {
			return false;
		}

		multi_size<N> size;
		table_count_size<Args...>::call(size.data_wrap());
		_size = size;
		_data.resize(_size.value(0), nullptr);
		return true;
	}

private:
	std::vector<Call> _data;
	multi_size<N> _size;

};

template <typename Call>
class table_data<Call, 0> {
public:
	inline table_data_wrap<Call, 0> data_wrap() {
		return table_data_wrap<Call, 0>(&_call, multi_size_wrap<0>(nullptr));
	}

	inline Call &operator[](multi_index<0> index) {
		return _call;
	}

	inline bool changed() const {
		return false;
	}

private:
	Call _call = nullptr;

};

template <typename Call, typename ...Args>
struct table_fill_entry_helper;

template <typename Call, typename Arg, typename ...Args>
struct table_fill_entry_helper<Call, Arg, Args...> {
	static constexpr int N = sizeof...(Args) + 1;

	static inline bool call(table_data_wrap<Call, N> table, multi_index_wrap<N> index, Call &fill) {
		auto start = index.current();
		for (auto i = start, count = table.size(); i != count; ++i) {
			auto foundGoodType = good(is_virtual_argument<Arg>(), start, index.current());
			if (foundGoodType) {
				index.current() = i;
				if (table_fill_entry_helper<Call, Args...>::call(table[i], index.subindex(), fill)) {
					return true;
				}
			}
		}
		index.current() = start;
		return false;
	}

	static inline bool good(std::integral_constant<bool,false>, int start, int current) {
		return (start == current);
	}
	static inline bool good(std::integral_constant<bool,true>, int start, int current) {
		using BaseObject = typename base::type_traits<Arg>::pointed_type;
		auto &entries = BaseObject::virtual_object_get_child_entries();
		return (start == current) || entries[start].check_is_parent(entries[current]);
	}

};

template <typename Call>
struct table_fill_entry_helper<Call> {
	static inline bool call(table_data_wrap<Call, 0> table, multi_index_wrap<0> index, Call &fill) {
		if (auto overrideMethod = table[index]) {
			fill = overrideMethod;
			return true;
		}
		return false;
	}
};

template <typename Call, int N>
struct table_fill_entry;

template <typename ReturnType, int N, typename BaseMethod, typename ...Args>
struct table_fill_entry<ReturnType(*)(BaseMethod*, Args...), N> {
	using Call = ReturnType(*)(BaseMethod*, Args...);
	static inline void call(table_data_wrap<Call, N> table, multi_index_wrap<N> index, Call &fill) {
		table_fill_entry_helper<Call, Args...>::call(table, index, fill);
	}
};

template <typename Call, int N>
inline void fill_entry(table_data_wrap<Call, N> table, multi_index_wrap<N> index, Call &fill) {
	return virtual_methods::table_fill_entry<Call, N>::call(table, index, fill);
}

template <int M, typename ...ConcreteArgs>
struct override_key_collector_helper;

template <int M, typename ConcreteArg, typename ...ConcreteArgs>
struct override_key_collector_helper<M, ConcreteArg, ConcreteArgs...> {
	static inline void call(int **indices) {
		setValue(is_virtual_argument<ConcreteArg>(), indices);
		override_key_collector_helper<M + 1, ConcreteArgs...>::call(indices);
	}

	static inline void setValue(std::integral_constant<bool,false>, int **indices) {
		indices[M] = nullptr;
	}
	static inline void setValue(std::integral_constant<bool,true>, int **indices) {
		using ConcreteObject = typename base::type_traits<ConcreteArg>::pointed_type;
		using IsParentCheckStruct = is_parent<ConcreteObject>;
		using IsParentCheckPointer = decltype(&IsParentCheckStruct::check);
		using override_key_collector_dont_optimize_away = dont_optimize_away_struct<IsParentCheckPointer, &IsParentCheckStruct::check>;
		override_key_collector_dont_optimize_away dont_optimize_away_object;
		(void)dont_optimize_away_object;

		// Check that is_parent<> can be instantiated.
		// So every ConcreteObject is a valid child of virtual_object<>.
		dont_optimize_away(reinterpret_cast<void*>(&IsParentCheckStruct::check));
		indices[M] = &ConcreteObject::virtual_object_child_index_static();
	}

};

template <int M>
struct override_key_collector_helper<M> {
	static inline void call(int **indices) {
	}
};

template <typename CallSignature>
struct override_key_collector;

template <typename ReturnType, typename BaseMethod, typename ...ConcreteArgs>
struct override_key_collector<ReturnType(*)(BaseMethod, ConcreteArgs...)> {
	static inline void call(int **indices) {
		override_key_collector_helper<0, ConcreteArgs...>::call(indices);
	}
};

template <int N>
class override_key {
public:
	inline multi_index<N> value() const {
		multi_index<N> result;
		for (int i = 0; i != N; ++i) {
			auto pointer = _indices[i];
			result._indices[i] = (pointer ? *pointer : 0);
		}
		return result;
	}

	friend inline bool operator<(const override_key &k1, const override_key &k2) {
		for (int i = 0; i != N; ++i) {
			auto pointer1 = k1._indices[i], pointer2 = k2._indices[i];
			if (pointer1 < pointer2) {
				return true;
			} else if (pointer1 > pointer2) {
				return false;
			}
		}
		return false;
	}

	template <typename CallSignature>
	inline void collect() {
		override_key_collector<CallSignature>::call(_indices);
	}

private:
	int *_indices[N];

};

template <typename BaseMethod, typename ConcreteMethod, typename CallSignature, typename ...Args>
struct static_cast_helper;

template <typename BaseMethod, typename ConcreteMethod, typename ReturnType, typename ...ConcreteArgs, typename ...Args>
struct static_cast_helper<BaseMethod, ConcreteMethod, ReturnType(*)(BaseMethod *, ConcreteArgs...), Args...> {
	static inline ReturnType call(BaseMethod *context, Args ...args) {
		return ConcreteMethod::call(context, static_cast<ConcreteArgs>(args)...);
	}
};

} // namespace virtual_methods

// This is a base class for all your virtual methods.
// It dispatches a call to one of the registered virtual_overrides
// or calls the fallback method of the BaseMethod class.
template <typename BaseMethod, typename ReturnType, typename ...Args>
class virtual_method {
	static constexpr int N = sizeof...(Args);
	using virtual_method_call = ReturnType(*)(BaseMethod *context, Args... args);

public:
	inline ReturnType call(Args... args) {
		auto context = static_cast<BaseMethod*>(this);
		auto index = virtual_methods::multi_index<N>::collect(args...);
		auto &table = virtual_method_prepare_table();
		auto &entry = table[index];
		if (!entry) {
			virtual_methods::fill_entry(table.data_wrap(), index.data_wrap(), entry);
			if (!entry) {
				entry = &virtual_method::virtual_method_base_instance;
			}
		}
		return (*entry)(context, args...);
	}

private:
	// This map of methods contains only the original registered overrides.
	using virtual_method_override_key = virtual_methods::override_key<N>;
	using virtual_method_override_map = std::map<virtual_method_override_key, virtual_method_call>;
	static inline virtual_method_override_map &virtual_method_get_override_map() {
		static virtual_method_override_map override_map;
		return override_map;
	}

	// This method generates and returns a virtual table which holds a method
	// for any child in the hierarchy or nullptr if none of the virtual_overrides fit.
	using virtual_method_table_data = virtual_methods::table_data<virtual_method_call, N>;
	static inline virtual_method_table_data &virtual_method_get_table_data() {
		static virtual_method_table_data virtual_table;
		return virtual_table;
	}

	static inline virtual_method_table_data &virtual_method_prepare_table() {
		auto &virtual_table = virtual_method_get_table_data();
		if (virtual_table.template changed<Args...>()) {
			virtual_methods::first_dispatch_fired(true);

			// The class hierarchy has changed - we need to generate the virtual table once again.
			// All other handlers will be placed if they're called.
			for (auto &i : virtual_method_get_override_map()) {
				virtual_table[i.first.value()] = i.second;
			}
		}
		return virtual_table;
	}

	static ReturnType virtual_method_base_instance(BaseMethod *context, Args... args) {
		return BaseMethod::default_call(context, args...);
	}

	template <typename ConcreteMethod>
	static ReturnType virtual_method_override_instance(BaseMethod *context, Args... args) {
		return virtual_methods::static_cast_helper<BaseMethod, ConcreteMethod, decltype(&ConcreteMethod::call), Args...>::call(context, args...);
	}

	template <typename ConcreteMethod>
	static inline void virtual_method_register_override() {
		auto call = &virtual_method_override_instance<ConcreteMethod>;

		virtual_methods::override_key<N> key;
		key.template collect<decltype(&ConcreteMethod::call)>();

		virtual_method_get_override_map()[key] = call;
	}

	template <typename ConcreteMethod, typename OtherBaseMethod>
	friend class virtual_override;

};

template <typename ConcreteMethod, typename BaseMethod>
class virtual_override {
protected:
	virtual ~virtual_override() {
		virtual_methods::dont_optimize_away(&_virtual_override_registrator);
	}

private:
	class virtual_override_registrator {
	public:
		inline virtual_override_registrator() {
			Assert(!virtual_methods::first_dispatch_fired());
			BaseMethod::template virtual_method_register_override<ConcreteMethod>();
		}

	};
	static virtual_override_registrator _virtual_override_registrator;
	using virtual_override_dont_optimize_away_registrator = virtual_methods::dont_optimize_away_struct<virtual_override_registrator*, &_virtual_override_registrator>;

};

template <typename ConcreteMethod, typename BaseMethod>
typename virtual_override<ConcreteMethod, BaseMethod>::virtual_override_registrator virtual_override<ConcreteMethod, BaseMethod>::_virtual_override_registrator = {};

} // namespace base

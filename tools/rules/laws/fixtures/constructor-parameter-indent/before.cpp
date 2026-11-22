struct Base {
	explicit Base(int) {}
};

struct Derived : Base {
	Derived(int first, int second);
};

Derived::Derived(
		int first,
		int second)
: Base(first + second) {
}

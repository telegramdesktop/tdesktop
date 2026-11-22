void setup() {
	auto callback = [=](
		int value
	) mutable {
		use(value);
		notify();
	};
}

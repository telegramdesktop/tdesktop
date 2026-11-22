void setup() {
	auto label = rpl::single(
		loading()
	) | rpl::then(
		std::move(progress)
	) | rpl::distinct_until_changed();
}

int choose(bool a, bool b, bool c) {
	return a
#if 1
		&& c
#endif
		? 1
		: 0;
}

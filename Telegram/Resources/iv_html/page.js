var IV = {
	notify: function(message) {
		if (window.external && window.external.invoke) {
			window.external.invoke(JSON.stringify(message));
		}
	},
	frameClickHandler: function(e) {
		var target = e.target;
		var context = '';
		while (target) {
			if (target.tagName == 'AUDIO' || target.tagName == 'VIDEO') {
				return;
			}
			if (context === ''
				&& target.hasAttribute
				&& target.hasAttribute('data-context')) {
				context = String(target.getAttribute('data-context'));
			}
			if (target.tagName == 'A') {
				break;
			}
			target = target.parentNode;
		}
		if (!target || !target.hasAttribute('href')) {
			return;
		}
		var base = document.createElement('A');
		base.href = window.location.href;
		if (base.origin != target.origin
			|| base.pathname != target.pathname
			|| base.search != target.search) {
			IV.notify({
				event: 'link_click',
				url: target.href,
				context: context,
			});
		} else if (target.hash.length < 2) {
			IV.jumpToHash('');
		} else {
			IV.jumpToHash(target.hash.substr(1));
		}
		e.preventDefault();
	},
	jumpToHash: function (hash, instant) {
		var current = IV.computeCurrentState();
		current.hash = hash;
		window.history.replaceState(current, '');
		if (hash == '') {
			IV.scrollTo(0, instant);
			return;
		}

		var element = document.getElementsByName(hash)[0];
		if (element) {
			var y = 0;
			while (element && !element.classList.contains('page-scroll')) {
				y += element.offsetTop;
				element = element.offsetParent;
			}
			IV.scrollTo(y, instant);
		}
	},
	frameKeyDown: function (e) {
		const keyW = (e.key === 'w')
			|| (e.code === 'KeyW')
			|| (e.keyCode === 87);
		const keyQ = (e.key === 'q')
			|| (e.code === 'KeyQ')
			|| (e.keyCode === 81);
		const keyM = (e.key === 'm')
			|| (e.code === 'KeyM')
			|| (e.keyCode === 77);
		if ((e.metaKey || e.ctrlKey) && (keyW || keyQ || keyM)) {
			e.preventDefault();
			IV.notify({
				event: 'keydown',
				modifier: e.ctrlKey ? 'ctrl' : 'cmd',
				key: keyW ? 'w' : keyQ ? 'q' : 'm',
			});
		} else if (e.key === 'Escape' || e.keyCode === 27) {
			e.preventDefault();
			IV.notify({
				event: 'keydown',
				key: 'escape',
			});
		}
	},
	frameMouseEnter: function (e) {
		IV.notify({ event: 'mouseenter' });
	},
	frameMouseUp: function (e) {
		IV.notify({ event: 'mouseup' });
	},
	lastScrollTop: 0,
	frameScrolled: function (e) {
		const was = IV.lastScrollTop;
		IV.lastScrollTop = IV.findPageScroll().scrollTop;
		IV.updateJumpToTop(was < IV.lastScrollTop);
	},
	updateJumpToTop: function (scrolledDown) {
		if (IV.lastScrollTop < 100) {
			document.getElementById('bottom_up').classList.add('hidden');
		} else if (scrolledDown && IV.lastScrollTop > 200) {
			document.getElementById('bottom_up').classList.remove('hidden');
		}
	},
	updateStyles: function (styles) {
		if (IV.styles !== styles) {
			IV.styles = styles;
			document.getElementsByTagName('html')[0].style = styles;
		}
	},
	toggleChannelJoined: function (id, joined) {
		const channels = document.getElementsByClassName('channel');
		const full = 'channel' + id;
		for (var i = 0; i < channels.length; ++i) {
			const channel = channels[i];
			if (String(channel.getAttribute('data-context')) === full) {
				channel.classList.toggle('joined', joined);
			}
		}
	},
	slideshowSlide: function(el, delta) {
		var dir = window.getComputedStyle(el, null).direction || 'ltr';
		var marginProp = dir == 'rtl' ? 'marginRight' : 'marginLeft';
		if (delta) {
			var form = el.parentNode.firstChild;
			var s = form.s;
			const next = +s.value + delta;
			s.value = (next == s.length) ? 0 : (next == -1) ? (s.length - 1) : next;
			s.forEach(function(el){ el.checked && el.parentNode.scrollIntoView && el.parentNode.scrollIntoView({behavior: 'smooth', block: 'center', inline: 'center'}); });
			form.nextSibling.firstChild.style[marginProp] = (-100 * s.value) + '%';
		} else {
			el.form.nextSibling.firstChild.style[marginProp] = (-100 * el.value) + '%';
		}
		return false;
	},
	initPreBlocks: function() {
		if (!hljs) {
			return;
		}
		var pres = document.getElementsByTagName('pre');
		for (var i = 0; i < pres.length; i++) {
			if (pres[i].hasAttribute('data-language')) {
				hljs.highlightBlock(pres[i]);
			}
		}
	},
	initEmbedBlocks: function() {
		var iframes = document.getElementsByTagName('iframe');
		for (var i = 0; i < iframes.length; i++) {
			(function(iframe) {
				window.addEventListener('message', function(event) {
					if (event.source !== iframe.contentWindow ||
							event.origin != window.origin) {
						return;
					}
					try {
						var data = JSON.parse(event.data);
					} catch(e) {
						var data = {};
					}
					if (data.eventType == 'resize_frame') {
						if (data.eventData.height) {
							iframe.style.height = data.eventData.height + 'px';
						}
					}
				}, false);
			})(iframes[i]);
		}
	},
	addRipple: function (button, x, y) {
		const ripple = document.createElement('span');
		ripple.classList.add('ripple');

		const inner = document.createElement('span');
		inner.classList.add('inner');
		x -= button.offsetLeft;
		y -= button.offsetTop;

		const mx = button.clientWidth - x;
		const my = button.clientHeight - y;
		const sq1 = x * x + y * y;
		const sq2 = mx * mx + y * y;
		const sq3 = x * x + my * my;
		const sq4 = mx * mx + my * my;
		const radius = Math.sqrt(Math.max(sq1, sq2, sq3, sq4));

		inner.style.width = inner.style.height = `${2 * radius}px`;
		inner.style.left = `${x - radius}px`;
		inner.style.top = `${y - radius}px`;
		inner.classList.add('inner');

		ripple.addEventListener('animationend', function (e) {
			if (e.animationName === 'fadeOut') {
				ripple.remove();
			}
		});

		ripple.appendChild(inner);
		button.appendChild(ripple);
	},
	stopRipples: function (button) {
		const id = button.id ? button.id : button;
		if (IV.frozenRipple === id) {
			return;
		}
		button = document.getElementById(id);
		const ripples = button.getElementsByClassName('ripple');
		for (var i = 0; i < ripples.length; ++i) {
			const ripple = ripples[i];
			if (!ripple.classList.contains('hiding')) {
				ripple.classList.add('hiding');
			}
		}
	},
	clearFrozenRipple: function () {
		if (IV.frozenRipple) {
			const button = document.getElementById(IV.frozenRipple);
			IV.frozenRipple = null;
			if (button) {
				IV.stopRipples(button);
			}
		}
	},
	init: function () {
		IV.platform = window.navigator.platform.toLowerCase();
		IV.mac = IV.platform.startsWith('mac');
		IV.win = IV.platform.startsWith('win');

		window.history.replaceState(IV.computeCurrentState(), '');
		IV.lastScrollTop = window.history.state.scroll;
		IV.findPageScroll().onscroll = IV.frameScrolled;

		const buttons = document.getElementsByClassName('fixed_button');
		for (let i = 0; i < buttons.length; ++i) {
			const button = buttons[i];
			button.addEventListener('mousedown', function (e) {
				IV.addRipple(e.currentTarget, e.clientX, e.clientY);
			});
			button.addEventListener('mouseup', function (e) {
				const id = e.currentTarget.id;
				setTimeout(function () {
					IV.stopRipples(id);
				}, 0);
			});
			button.addEventListener('mouseleave', function (e) {
				IV.stopRipples(e.currentTarget);
			});
		}
		IV.initMedia();
		IV.notify({ event: 'ready' });
	},
	initMedia: function () {
		const photos = document.getElementsByClassName('photo');
		for (let i = 0; i < photos.length; ++i) {
			const photo = photos[i];
			if (photo.classList.contains('loaded')) {
				continue;
			}

			const url = photo.style.backgroundImage;
			if (!url || url.length < 7) {
				continue;
			}
			var img = new Image();
			img.onload = function () {
				photo.classList.add('loaded');
			}
			img.src = url.substr(5, url.length - 7);
			if (img.complete) {
				img.onload();
			}
		}
		const videos = document.getElementsByTagName('video');
		for (let i = 0; i < videos.length; ++i) {
			const video = videos[i];
			if (video.classList.contains('loaded')) {
				continue;
			}
			video.addEventListener('canplay', function () {
				video.classList.add('loaded');
			});
		}
	},
	showTooltip: function (text) {
		var toast = document.createElement('div');
		toast.classList.add('toast');
		toast.textContent = text;
		document.body.appendChild(toast);
		setTimeout(function () {
			toast.classList.add('hiding');
		}, 2000);
		setTimeout(function () {
			document.body.removeChild(toast);
		}, 3000);
	},
	scrollTo: function (y, instant) {
		if (y < 200) {
			document.getElementById('bottom_up').classList.add('hidden');
		}
		IV.findPageScroll().scrollTo({
			top: y || 0,
			behavior: instant ? 'instant' : 'smooth'
		});
	},
	menu: function (button) {
		IV.frozenRipple = button.id;
		const state = this.computeCurrentState();
		IV.notify({ event: 'menu', index: state.index, hash: state.hash });
	},

	computeCurrentState: function () {
		var now = IV.findPageScroll();
		return {
			position: IV.position,
			index: IV.index,
			hash: ((!window.history.state
				|| window.history.state.hash === undefined)
				? window.location.hash.substr(1)
				: window.history.state.hash),
			scroll: now ? now.scrollTop : 0
		};
	},
	navigateTo: function (index, hash) {
		if (!index && !IV.index) {
			IV.navigateToDOM(IV.index, hash);
			return;
		}
		IV.pending = [index, hash];
		if (!IV.cache[index]) {
			IV.cache[index] = { loading: true };

			let xhr = new XMLHttpRequest();
			xhr.onload = function () {
				IV.cache[index].loading = false;
				IV.cache[index].content = xhr.responseText;
				if (IV.pending && IV.pending[0] == index) {
					IV.navigateToLoaded(index, IV.pending[1]);
				}
			}

			xhr.open('GET', 'page' + index + '.json');
			xhr.send();
		} else if (IV.cache[index].dom) {
			IV.navigateToDOM(index, hash);
		} else if (IV.cache[index].content) {
			IV.navigateToLoaded(index, hash);
		}
	},

	navigateToLoaded: function (index, hash) {
		if (IV.cache[index].dom) {
			IV.navigateToDOM(index, hash);
		} else {
			var data = JSON.parse(IV.cache[index].content);
			var el = document.createElement('div');
			el.className = 'page-scroll';
			el.innerHTML = '<div class="page-slide"><article>'
				+ data.html
				+ '</article></div>';
			el.onscroll = IV.frameScrolled;
			IV.cache[index].dom = el;

			IV.navigateToDOM(index, hash);
			eval(data.js);
		}
	},
	navigateToDOM: function (index, hash) {
		IV.pending = null;
		if (IV.index == index) {
			IV.jumpToHash(hash, IV.mac);
			return;
		}
		window.history.replaceState(IV.computeCurrentState(), '');

		IV.position = IV.position + 1;
		window.history.pushState(
			{ position: IV.position, index: index, hash: hash },
			'',
			'page' + index + '.html' + (hash.length ? '#' + hash : ''));
		IV.showDOM(index, hash);
	},
	findPageScroll: function () {
		var all = document.getElementsByClassName('page-scroll');
		for (i = 0; i < all.length; ++i) {
			if (!all[i].classList.contains('hidden-left')
				&& !all[i].classList.contains('hidden-right')) {
				return all[i];
			}
		}
		return null;
	},
	showDOM: function (index, hash, scroll) {
		IV.pending = null;
		if (IV.index != index) {
			var initial = !window.history.state
				|| window.history.state.position === undefined;
			var back = initial
				|| IV.position > window.history.state.position;
			IV.position = initial ? 0 : window.history.state.position;

			var now = IV.cache[index].dom;
			var was = IV.findPageScroll();
			if (!IV.cache[IV.index]) {
				IV.cache[IV.index] = {};
			}
			IV.cache[IV.index].dom = was;
			was.parentNode.appendChild(now);
			if (scroll !== undefined) {
				now.scrollTop = scroll;
			}

			now.classList.add(back ? 'hidden-left' : 'hidden-right');
			now.classList.remove(back ? 'hidden-right' : 'hidden-left');
			now.firstChild.getAnimations().forEach(
				(animation) => animation.finish());

			if (!was.listening) {
				was.listening = true;
				was.firstChild.addEventListener('transitionend', function (e) {
					if (was.classList.contains('hidden-left')
						|| was.classList.contains('hidden-right')) {
						if (was.parentNode) {
							was.parentNode.removeChild(was);
						}
					}
				});
			}

			was.classList.add(back ? 'hidden-right' : 'hidden-left');
			now.classList.remove(back ? 'hidden-left' : 'hidden-right');

			var topBack = document.getElementById('top_back');
			if (!IV.position) {
				topBack.classList.add('hidden');
			} else {
				topBack.classList.remove('hidden');
			}
			IV.index = index;
			IV.initMedia();
			if (scroll === undefined) {
				IV.jumpToHash(hash, true);
			} else {
				IV.lastScrollTop = scroll;
				IV.updateJumpToTop(true);
			}
		} else if (scroll !== undefined) {
			IV.scrollTo(scroll, IV.mac);
			IV.lastScrollTop = scroll;
			IV.updateJumpToTop(true);
		} else {
			IV.jumpToHash(hash, IV.mac);
		}
	},
	back: function () {
        window.history.back();
	},

	cache: {},
	index: 0,
	position: 0
};

document.onclick = IV.frameClickHandler;
document.onkeydown = IV.frameKeyDown;
document.onmouseenter = IV.frameMouseEnter;
document.onmouseup = IV.frameMouseUp;
window.onmessage = IV.postMessageHandler;

window.addEventListener('popstate', function (e) {
	if (e.state) {
		IV.showDOM(e.state.index, e.state.hash, e.state.scroll);
	}
});

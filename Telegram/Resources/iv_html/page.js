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
			if (target.id == 'menu_page_blocker') {
				IV.notify({ event: 'menu_page_blocker_click' });
				IV.menuShown(false);
				return;
			}
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
			IV.jumpToHash(decodeURIComponent(target.hash.substr(1)));
		}
		e.preventDefault();
	},
	getElementTop: function (element) {
		var top = 0;
		while (element && !element.classList.contains('page-scroll')) {
			top += element.offsetTop;
			element = element.offsetParent;
		}
		return top;
	},
	jumpToHash: function (hash, instant) {
		var current = IV.computeCurrentState();
		current.hash = hash;
		window.history.replaceState(
			current,
			'',
			'page' + IV.index + '.html');
		if (hash == '') {
			IV.scrollTo(0, instant);
			return;
		}

		var element = document.getElementsByName(hash)[0];
		if (element) {
			IV.scrollTo(IV.getElementTop(element), instant);
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
			if (IV.position) {
				window.history.back();
			} else {
				IV.notify({
					event: 'keydown',
					key: 'escape',
				});
			}
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
		IV.checkVideos();
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
		IV.channelsJoined['channel' + id] = joined;
		IV.checkChannelButtons();
	},
	checkChannelButtons: function() {
		const channels = document.getElementsByClassName('channel');
		for (var i = 0; i < channels.length; ++i) {
			const channel = channels[i];
			const full = String(channel.getAttribute('data-context'));
			const value = IV.channelsJoined[full];
			if (value !== undefined) {
				channel.classList.toggle('joined', value);
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
		button = document.getElementById(id);
		const ripples = button.getElementsByClassName('ripple');
		for (var i = 0; i < ripples.length; ++i) {
			const ripple = ripples[i];
			if (!ripple.classList.contains('hiding')) {
				ripple.classList.add('hiding');
			}
		}
	},
	init: function () {
		var current = IV.computeCurrentState();
		window.history.replaceState(current, '', IV.pageUrl(0));
		IV.jumpToHash(current.hash, true);

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

		IV.forceScrollFocus();
		IV.frameScrolled();
	},
	initMedia: function () {
		var scroll = IV.findPageScroll();
		const photos = scroll.getElementsByClassName('photo');
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
				photo.classList.add('loaded');
				IV.stopAnimations(photo);
			}
		}
		IV.videos = [];
		const videos = scroll.getElementsByClassName('video');
		for (let i = 0; i < videos.length; ++i) {
			const element = videos[i];
			IV.videos.push({
				element: element,
				src: String(element.getAttribute('data-src')),
				autoplay: (element.getAttribute('data-autoplay') == '1'),
				loop: (element.getAttribute('data-loop') == '1'),
				small: (element.getAttribute('data-small') == '1'),
				filled: (element.firstChild
					&& element.firstChild.tagName == 'VIDEO'),
			});
		}
	},
	checkVideos: function () {
		const visibleTop = IV.lastScrollTop;
		const visibleBottom = visibleTop + IV.findPageScroll().offsetHeight;
		const videos = IV.videos;
		for (let i = 0; i < videos.length; ++i) {
			const video = videos[i];
			const element = video.element;
			const wrap = element.offsetParent; // video-wrap
			const top = IV.getElementTop(wrap);
			const bottom = top + wrap.offsetHeight;
			if (top < visibleBottom && bottom > visibleTop) {
				if (!video.created) {
					video.created = new Date();
					video.loaded = false;
					element.innerHTML = '<video muted class="'
						+ (video.small ? 'video-small' : '')
						+ '"'
						+ (video.autoplay
							? ' preload="auto" autoplay'
							: (video.small
								? ''
								: ' controls'))
						+ (video.loop ? ' loop' : '')
						+ ' oncanplay="IV.checkVideos();"'
						+ ' onloadeddata="IV.checkVideos();">'
							+ '<source src="'
							+ video.src
							+ '" type="video/mp4" />'
						+ '</video>';
					var media = element.firstChild;
					media.oncontextmenu = function () { return false; };
					media.oncanplay = IV.checkVideos;
					media.onloadeddata = IV.checkVideos;
				}
			} else if (video.created && video.autoplay) {
				video.created = false;
				element.innerHTML = '';
			}
			if (video.created && !video.loaded) {
				var media = element.firstChild;
				const HAVE_CURRENT_DATA = 2;
				if (media && media.readyState >= HAVE_CURRENT_DATA) {
					video.loaded = true;
					media.classList.add('loaded');
					if ((new Date() - video.created) < 100) {
						IV.stopAnimations(media);
					}
				}
			}
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
	pageUrl: function (index, hash) {
		var result = 'page' + index + '.html';
		if (hash) {
			result += '#' + hash;
		}
		return result;
	},
	navigateTo: function (index, hash) {
		if (!index && !IV.index) {
			IV.navigateToDOM(IV.index, hash);
			return;
		}
		IV.pending = [index, hash];
		if (!IV.cache[index]) {
			IV.loadPage(index);
		} else if (IV.cache[index].dom) {
			IV.navigateToDOM(index, hash);
		} else if (IV.cache[index].content) {
			IV.navigateToLoaded(index, hash);
		}
	},
	applyUpdatedContent: function (index) {
		if (IV.index != index) {
			IV.cache[index].contentUpdated = (IV.cache[index].dom !== undefined);
			return;
		}
		var data = JSON.parse(IV.cache[index].content);
		var article = function (el) {
			return el.getElementsByTagName('article')[0];
		};
		var from = article(IV.findPageScroll());
		var to = article(IV.makeScrolledContent(data.html));
		morphdom(from, to, {
			onBeforeElUpdated: function (fromEl, toEl) {
				if (fromEl.classList.contains('video')
					&& toEl.classList.contains('video')
					&& fromEl.hasAttribute('data-src')
					&& toEl.hasAttribute('data-src')
					&& (fromEl.getAttribute('data-src')
						== toEl.getAttribute('data-src'))) {
					return false;
				} else if (fromEl.tagName == 'SECTION'
					&& fromEl.classList.contains('channel')
					&& fromEl.hasAttribute('data-context')
					&& toEl.tagName == 'SECTION'
					&& toEl.classList.contains('channel')
					&& toEl.hasAttribute('data-context')
					&& (String(fromEl.getAttribute('data-context'))
						== String(toEl.getAttribute('data-context')))) {
					return false;
				} else if (fromEl.classList.contains('loaded')) {
					toEl.classList.add('loaded');
				}
				return !fromEl.isEqualNode(toEl);
			}
		});
		IV.initMedia();
		eval(data.js);
	},
	loadPage: function (index) {
		if (!IV.cache[index]) {
			IV.cache[index] = {};
		}
		IV.cache[index].loading = true;

		let xhr = new XMLHttpRequest();
		xhr.onload = function () {
			IV.cache[index].loading = false;
			IV.cache[index].content = xhr.responseText;
			IV.applyUpdatedContent(index);
			if (IV.pending && IV.pending[0] == index) {
				IV.navigateToLoaded(index, IV.pending[1]);
			}
			if (IV.cache[index].reloadPending) {
				IV.cache[index].reloadPending = false;
				IV.reloadPage(index);
			}
		}

		xhr.open('GET', 'page' + index + '.json');
		xhr.send();
	},
	reloadPage: function (index) {
		if (IV.cache[index] && IV.cache[index].loading) {
			IV.cache[index].reloadPending = true;
			return;
		}
		IV.loadPage(index);
	},

	makeScrolledContent: function (html) {
		var result = document.createElement('div');
		result.className = 'page-scroll';
		result.tabIndex = '-1';
		result.innerHTML = '<div class="page-slide"><article>'
			+ html
			+ '</article></div>';
		result.onscroll = IV.frameScrolled;
		return result;
	},
	navigateToLoaded: function (index, hash) {
		if (IV.cache[index].dom) {
			IV.navigateToDOM(index, hash);
		} else {
			var data = JSON.parse(IV.cache[index].content);
			IV.cache[index].dom = IV.makeScrolledContent(data.html);

			IV.navigateToDOM(index, hash);
			eval(data.js);
		}
	},
	navigateToDOM: function (index, hash) {
		IV.pending = null;
		if (IV.index == index) {
			IV.jumpToHash(hash);
			IV.forceScrollFocus();
			return;
		}
		window.history.replaceState(
			IV.computeCurrentState(),
			'',
			IV.pageUrl(IV.index));

		IV.position = IV.position + 1;
		window.history.pushState(
			{ position: IV.position, index: index, hash: hash },
			'',
			IV.pageUrl(index));
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
				setTimeout(function () {
					// When returning by history.back to an URL with a hash
					// for the first time browser forces the scroll to the
					// hash instead of the saved scroll position.
					//
					// This workaround prevents incorrect scroll position.
					now.scrollTop = scroll;
				}, 0);
			}

			now.classList.add(back ? 'hidden-left' : 'hidden-right');
			now.classList.remove(back ? 'hidden-right' : 'hidden-left');
			IV.stopAnimations(now.firstChild);

			if (!was.listening) {
				was.listening = true;
				was.firstChild.addEventListener('transitionend', function (e) {
					if (was.classList.contains('hidden-left')
						|| was.classList.contains('hidden-right')) {
						if (was.parentNode) {
							was.parentNode.removeChild(was);
							var videos = was.getElementsByClassName('video');
							for (var i = 0; i < videos.length; ++i) {
								videos[i].innerHTML = '';
                            }
						}
					}
				});
			}

			was.classList.add(back ? 'hidden-right' : 'hidden-left');
			now.classList.remove(back ? 'hidden-left' : 'hidden-right');

			IV.index = index;
			IV.notify({
				event: 'location_change',
				index: IV.index,
				position: IV.position,
				hash: IV.computeCurrentState().hash,
			});
			if (IV.cache[index].contentUpdated) {
				IV.cache[index].contentUpdated = false;
				IV.applyUpdatedContent(index);
			} else {
				IV.initMedia();
			}
			IV.checkChannelButtons();
			if (scroll === undefined) {
				IV.jumpToHash(hash, true);
			} else {
				IV.lastScrollTop = scroll;
				IV.updateJumpToTop(true);
			}
		} else if (scroll !== undefined) {
			IV.scrollTo(scroll);
			IV.lastScrollTop = scroll;
			IV.updateJumpToTop(true);
		} else {
			IV.jumpToHash(hash);
		}

		IV.forceScrollFocus();
		IV.frameScrolled();
	},
	forceScrollFocus: function () {
		IV.findPageScroll().focus();
		setTimeout(function () {
			// Doesn't work on #hash-ed pages in Windows WebView2 otherwise.
			IV.findPageScroll().focus();
		}, 100);
	},
	stopAnimations: function (element) {
		element.getAnimations().forEach(
			(animation) => animation.finish());
	},
	back: function () {
        window.history.back();
	},
	menuShown: function (shown) {
		var already = document.getElementById('menu_page_blocker');
		if (already && shown) {
			return;
		} else if (already) {
			document.body.removeChild(already);
			return;
		} else if (!shown) {
			return;
		}
		var blocker = document.createElement('div');
		blocker.id = 'menu_page_blocker';
		document.body.appendChild(blocker);
	},

	videos: {},
	videosPlaying: {},

	cache: {},
	channelsJoined: {},
	index: 0,
	position: 0
};

document.onclick = IV.frameClickHandler;
document.onkeydown = IV.frameKeyDown;
document.onmouseenter = IV.frameMouseEnter;
document.onmouseup = IV.frameMouseUp;
document.onresize = IV.checkVideos;
window.onmessage = IV.postMessageHandler;
window.addEventListener('popstate', function (e) {
	if (e.state) {
		IV.showDOM(e.state.index, e.state.hash, e.state.scroll);
	}
});
document.addEventListener("DOMContentLoaded", IV.forceScrollFocus);

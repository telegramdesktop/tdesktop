(function() {
	'use strict';

	const root = document.getElementById('root');
	const header = document.getElementById('header');
	const frameShell = document.getElementById('frame-shell');
	const frameWrap = document.getElementById('frame-wrap');
	const disclosure = document.getElementById('disclosure');
	const footer = document.getElementById('footer');
	const buttonsWrap = document.getElementById('buttons-wrap');
	const buttons = document.getElementById('buttons');
	const badge = document.getElementById('badge');
	const menuBackdrop = document.getElementById('menu-backdrop');
	const menu = document.getElementById('menu');
	const menuList = document.getElementById('menu-list');
	const blocker = document.getElementById('blocker');
	const resizeHandles = Array.prototype.slice.call(
		document.querySelectorAll('.resize-handle'));
	const title = document.getElementById('title');
	const controls = {
		back: document.getElementById('back'),
		menu: document.getElementById('menu-toggle'),
		menuIcon: document.getElementById('menu-toggle-icon'),
		close: document.getElementById('close')
	};
	const shellState = {
		backVisible: false,
		menuVisible: false,
		badgeVisible: false,
		bottomText: '',
		isFullscreen: false,
		blocked: false,
		menuOpen: false,
		menuItems: [],
		buttons: {
			main: null,
			secondary: null
		}
	};
	if (window.TelegramDesktopWindowAlphaSupported === false) {
		root.classList.add('no-window-alpha');
	}
	const shellAssets = {
		icons: Object.create(null),
		titleMenuIcon: null,
		verifiedBadge: null,
		menuPalette: null
	};
	const shellToken = TDESKTOP_SHELL_TOKEN_PLACEHOLDER;
	const nativeMessageType = 'tdesktop_external_bot_webapp';
	const maxPendingEvents = 64;
	let iframe = null;
	let frameLoaded = false;
	let frameUrl = 'about:blank';
	let frameOrigin = '';
	let sameOrigin = false;
	let frameGeneration = 0;
	let reloadSupported = false;
	let reloadTimeout = null;
	let viewportScheduled = false;
	let resizeObserver = null;
	const pendingEvents = [];

	function normalizeEventData(eventData) {
		if (typeof eventData === 'string') {
			try {
				const parsed = JSON.parse(eventData);
				return parsed
					&& typeof parsed === 'object'
					&& !Array.isArray(parsed)
					? parsed
					: {};
			} catch (e) {
				return {};
			}
		}
		return eventData
			&& typeof eventData === 'object'
			&& !Array.isArray(eventData)
			? eventData
			: {};
	}

	function isShellOrigin() {
		return window.location.protocol === 'https:'
			&& window.location.hostname === 'web.telegram.org'
			&& (!window.location.port || window.location.port === '443');
	}

	function originFromUrl(url) {
		try {
			const origin = new URL(url, window.location.href).origin;
			return origin && origin !== 'null' ? origin : '';
		} catch (e) {
			return '';
		}
	}

	function invokeNative(source, eventType, eventData, origin) {
		if (!window.external
			|| typeof window.external.invoke !== 'function'
			|| typeof shellToken !== 'string'
			|| !shellToken
			|| typeof eventType !== 'string') {
			return;
		}
		if (source === 'shell' && !isShellOrigin()) {
			return;
		}
		window.external.invoke(JSON.stringify({
			type: nativeMessageType,
			source: source,
			token: shellToken,
			origin: origin || window.location.origin,
			eventType: eventType,
			eventData: normalizeEventData(eventData)
		}));
	}

	function invokeShell(eventType, eventData) {
		invokeNative('shell', eventType, eventData);
	}

	function invokeWebApp(eventType, eventData, origin) {
		invokeNative('webapp', eventType, eventData, origin);
	}

	function sendToFrame(eventType, eventData, generation) {
		if (!iframe
			|| !iframe.contentWindow
			|| generation !== frameGeneration) {
			return;
		}
		if (sameOrigin && !frameOrigin) {
			return;
		}
		const targetOrigin = sameOrigin ? frameOrigin : '*';
		iframe.contentWindow.postMessage(JSON.stringify({
			eventType: eventType,
			eventData: eventData || {}
		}), targetOrigin);
	}

	function postToFrame(eventType, eventData) {
		const generation = frameGeneration;
		if (!iframe || !iframe.contentWindow || !frameLoaded) {
			pendingEvents.push({
				generation: generation,
				eventType: eventType,
				eventData: eventData || {}
			});
			while (pendingEvents.length > maxPendingEvents) {
				pendingEvents.shift();
			}
			return;
		}
		sendToFrame(eventType, eventData, generation);
	}

	function shellPointerPayload(event, extra) {
		const payload = {
			button: event.button,
			x: event.clientX,
			y: event.clientY,
			rootX: event.screenX,
			rootY: event.screenY,
			timeStamp: Math.round(event.timeStamp || 0)
		};
		if (extra && typeof extra === 'object') {
			for (const key in extra) {
				payload[key] = extra[key];
			}
		}
		return payload;
	}

	function beginShellControl(command, event, extra) {
		if (shellState.blocked
			|| shellState.isFullscreen
			|| event.defaultPrevented
			|| !event.isTrusted
			|| event.button !== 0) {
			return;
		}
		closeMenu();
		invokeShell(command, shellPointerPayload(event, extra));
		event.preventDefault();
	}

	function beginShellMove(event) {
		const target = event.target;
		if (target
			&& target.closest
			&& target.closest('.title-control, #menu')) {
			return;
		}
		beginShellControl('shell_begin_move', event);
	}

	function beginShellResize(edge, event) {
		beginShellControl('shell_begin_resize', event, {
			edge: edge
		});
	}

	function flushPendingEvents() {
		const pending = pendingEvents.splice(0);
		for (const event of pending) {
			if (event.generation === frameGeneration) {
				sendToFrame(event.eventType, event.eventData, event.generation);
			}
		}
	}

	function sendViewportChanged() {
		if (!iframe) {
			return;
		}
		const height = Math.max(
			0,
			Math.round(frameShell.getBoundingClientRect().height));
		postToFrame('viewport_changed', {
			height: height,
			is_state_stable: true,
			is_expanded: true
		});
	}

	function scheduleViewport() {
		if (viewportScheduled) {
			return;
		}
		viewportScheduled = true;
		window.requestAnimationFrame(function() {
			viewportScheduled = false;
			sendViewportChanged();
		});
	}

	function setMetric(name, value) {
		if (typeof value === 'number' && Number.isFinite(value)) {
			root.style.setProperty(name, String(value) + 'px');
		}
	}

	function applyMetrics(data) {
		if (!data || typeof data !== 'object') {
			return;
		}
		setMetric('--shell-radius', data.shellRadius);
		setMetric('--shell-pad-top', data.shellPaddingTop);
		setMetric('--shell-pad-right', data.shellPaddingRight);
		setMetric('--shell-pad-bottom', data.shellPaddingBottom);
		setMetric('--shell-pad-left', data.shellPaddingLeft);
		setMetric('--shadow-pad-top', data.shadowPaddingTop);
		setMetric('--shadow-pad-right', data.shadowPaddingRight);
		setMetric('--shadow-pad-bottom', data.shadowPaddingBottom);
		setMetric('--shadow-pad-left', data.shadowPaddingLeft);
		setMetric('--header-height', data.headerHeight);
		setMetric('--title-pad-top', data.titlePaddingTop);
		setMetric('--title-pad-right', data.titlePaddingRight);
		setMetric('--title-pad-bottom', data.titlePaddingBottom);
		setMetric('--title-pad-left', data.titlePaddingLeft);
		setMetric('--badge-skip', data.badgeSkip);
		setMetric('--frame-radius', data.frameRadius);
		setMetric('--control-width', data.controlWidth);
		setMetric('--control-height', data.controlHeight);
		setMetric('--button-height', data.buttonHeight);
		setMetric('--button-gap-x', data.buttonGapX);
		setMetric('--button-gap-y', data.buttonGapY);
		setMetric('--disclosure-skip', data.disclosureSkip);
		setMetric('--footer-button-skip', data.footerButtonSkip);
		setMetric('--fullscreen-control-width', data.fullscreenControlWidth);
		setMetric('--fullscreen-control-height', data.fullscreenControlHeight);
		setMetric('--fullscreen-control-top', data.fullscreenControlTop);
		setMetric('--fullscreen-control-right', data.fullscreenControlRight);
		setMetric('--fullscreen-control-gap', data.fullscreenControlGap);
	}

	function colorForBackground(value) {
		if (!/^#[0-9a-f]{6}$/i.test(value || '')) {
			return null;
		}
		const red = parseInt(value.slice(1, 3), 16) / 255;
		const green = parseInt(value.slice(3, 5), 16) / 255;
		const blue = parseInt(value.slice(5, 7), 16) / 255;
		const luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
		return luminance > 0.5 ? '#000000' : '#ffffff';
	}

	function footerColorForBackground(value) {
		if (!/^#[0-9a-f]{6}$/i.test(value || '')) {
			return null;
		}
		const red = parseInt(value.slice(1, 3), 16) / 255;
		const green = parseInt(value.slice(3, 5), 16) / 255;
		const blue = parseInt(value.slice(5, 7), 16) / 255;
		const luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
		const contrast = 2.5;
		const textLuminance = (luminance > 0.5) ? 0 : 1;
		const adaptiveOpacity = (luminance - textLuminance + contrast) / contrast;
		const opacity = Math.max(0.5, Math.min(0.64, adaptiveOpacity));
		const channel = (luminance > 0.5) ? 0 : 255;
		return 'rgba('
			+ String(channel) + ', '
			+ String(channel) + ', '
			+ String(channel) + ', '
			+ String(opacity) + ')';
	}

	function titleControlColorsForBackground(value) {
		if (!/^#[0-9a-f]{6}$/i.test(value || '')) {
			return null;
		}
		const red = parseInt(value.slice(1, 3), 16) / 255;
		const green = parseInt(value.slice(3, 5), 16) / 255;
		const blue = parseInt(value.slice(5, 7), 16) / 255;
		const luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
		const contrast = 2.5;
		const textLuminance = (luminance > 0.5) ? 0 : 1;
		const adaptiveOpacity = (luminance - textLuminance + contrast) / contrast;
		const opacity = Math.max(0.5, Math.min(0.64, adaptiveOpacity));
		const channel = (luminance > 0.5) ? 0 : 255;
		return {
			fg: 'rgba('
				+ String(channel) + ', '
				+ String(channel) + ', '
				+ String(channel) + ', '
				+ String(opacity) + ')',
			ripple: 'rgba('
				+ String(channel) + ', '
				+ String(channel) + ', '
				+ String(channel) + ', '
				+ String(opacity * 0.1) + ')'
		};
	}

	function hexByte(value) {
		const text = Math.max(
			0,
			Math.min(255, Math.round(value))).toString(16);
		return (text.length < 2 ? '0' : '') + text;
	}

	function buttonRippleForBackground(value) {
		if (!/^#[0-9a-f]{6}$/i.test(value || '')) {
			return null;
		}
		const red = parseInt(value.slice(1, 3), 16);
		const green = parseInt(value.slice(3, 5), 16);
		const blue = parseInt(value.slice(5, 7), 16);
		const maximum = Math.max(red, green, blue);
		const minimum = Math.min(red, green, blue);
		const delta = maximum - minimum;
		let hue = 0;
		if (delta !== 0 && maximum === red) {
			hue = 60 * (((green - blue) / delta) % 6);
		} else if (delta !== 0 && maximum === green) {
			hue = 60 * ((blue - red) / delta + 2);
		} else if (delta !== 0) {
			hue = 60 * ((red - green) / delta + 4);
		}
		if (hue < 0) {
			hue += 360;
		}
		const saturation = maximum === 0 ? 0 : delta / maximum;
		const nextValue = Math.max(
			0,
			Math.min(255, maximum + (maximum > 128 ? -32 : 32)));
		const chroma = nextValue * saturation;
		const x = chroma * (1 - Math.abs((hue / 60) % 2 - 1));
		const m = nextValue - chroma;
		let nextRed = 0;
		let nextGreen = 0;
		let nextBlue = 0;
		if (hue < 60) {
			nextRed = chroma;
			nextGreen = x;
		} else if (hue < 120) {
			nextRed = x;
			nextGreen = chroma;
		} else if (hue < 180) {
			nextGreen = chroma;
			nextBlue = x;
		} else if (hue < 240) {
			nextGreen = x;
			nextBlue = chroma;
		} else if (hue < 300) {
			nextRed = x;
			nextBlue = chroma;
		} else {
			nextRed = chroma;
			nextBlue = x;
		}
		return '#'
			+ hexByte(nextRed + m)
			+ hexByte(nextGreen + m)
			+ hexByte(nextBlue + m);
	}

	function applyColors(data) {
		const next = (data && data.colors) ? data.colors : data;
		if (!next || typeof next !== 'object') {
			return;
		}
		if (next.bodyBg) {
			root.style.setProperty('--body-bg', next.bodyBg);
			const footerFg = footerColorForBackground(next.bodyBg);
			if (footerFg) {
				root.style.setProperty('--footer-fg', footerFg);
			}
		}
		if (next.titleBg) {
			root.style.setProperty('--title-bg', next.titleBg);
			const titleFg = colorForBackground(next.titleBg);
			if (titleFg) {
				root.style.setProperty('--title-fg', titleFg);
			}
			const titleControl = titleControlColorsForBackground(next.titleBg);
			if (titleControl) {
				root.style.setProperty('--title-control-fg', titleControl.fg);
				root.style.setProperty(
					'--title-control-ripple',
					titleControl.ripple);
			}
		}
		if (next.bottomBg) {
			root.style.setProperty('--bottom-bg', next.bottomBg);
		}
	}

	function applyAssets(data) {
		if (!data || typeof data !== 'object') {
			return;
		}
		if (data.icons && typeof data.icons === 'object') {
			shellAssets.icons = data.icons;
		}
		if (data.titleMenuIcon && typeof data.titleMenuIcon === 'object') {
			shellAssets.titleMenuIcon = data.titleMenuIcon;
		}
		if (data.verifiedBadge && typeof data.verifiedBadge === 'object') {
			shellAssets.verifiedBadge = data.verifiedBadge;
		}
		if (data.menuPalette && typeof data.menuPalette === 'object') {
			shellAssets.menuPalette = data.menuPalette;
			if (data.menuPalette.bg) {
				root.style.setProperty('--menu-bg', data.menuPalette.bg);
			}
			if (data.menuPalette.fg) {
				root.style.setProperty('--menu-fg', data.menuPalette.fg);
			}
			if (data.menuPalette.hoverBg) {
				root.style.setProperty(
					'--menu-hover-bg',
					data.menuPalette.hoverBg);
			}
			if (data.menuPalette.ripple) {
				root.style.setProperty('--menu-ripple', data.menuPalette.ripple);
			}
			if (data.menuPalette.separator) {
				root.style.setProperty(
					'--menu-separator',
					data.menuPalette.separator);
			}
			if (data.menuPalette.attention) {
				root.style.setProperty(
					'--menu-attention',
					data.menuPalette.attention);
			}
		}
		if (shellAssets.titleMenuIcon && shellAssets.titleMenuIcon.url) {
			controls.menuIcon.style.webkitMaskImage = 'url('
				+ shellAssets.titleMenuIcon.url + ')';
			controls.menuIcon.style.maskImage = 'url('
				+ shellAssets.titleMenuIcon.url + ')';
			if (shellAssets.titleMenuIcon.width) {
				controls.menuIcon.style.width
					= String(shellAssets.titleMenuIcon.width) + 'px';
			}
			if (shellAssets.titleMenuIcon.height) {
				controls.menuIcon.style.height
					= String(shellAssets.titleMenuIcon.height) + 'px';
			}
		}
		if (shellAssets.verifiedBadge && shellAssets.verifiedBadge.url) {
			badge.style.backgroundImage = 'url('
				+ shellAssets.verifiedBadge.url + ')';
			if (shellAssets.verifiedBadge.width) {
				badge.style.width = String(shellAssets.verifiedBadge.width) + 'px';
			}
			if (shellAssets.verifiedBadge.height) {
				badge.style.height = String(shellAssets.verifiedBadge.height) + 'px';
			}
			if (shellAssets.verifiedBadge.alt) {
				badge.setAttribute('aria-label', shellAssets.verifiedBadge.alt);
			}
		} else {
			badge.style.backgroundImage = '';
			badge.removeAttribute('aria-label');
		}
		applyChrome({});
		renderMenu();
	}

	function applyChrome(data) {
		if (!data || typeof data !== 'object') {
			return;
		}
		if (Object.prototype.hasOwnProperty.call(data, 'backVisible')) {
			shellState.backVisible = !!data.backVisible;
		}
		if (Object.prototype.hasOwnProperty.call(data, 'menuVisible')) {
			shellState.menuVisible = !!data.menuVisible;
		}
		if (Object.prototype.hasOwnProperty.call(data, 'badgeVisible')) {
			shellState.badgeVisible = !!data.badgeVisible;
		}
		controls.back.classList.toggle('hidden', !shellState.backVisible);
		controls.menu.classList.toggle('hidden', !shellState.menuVisible);
		controls.menu.disabled = !shellState.menuVisible
			|| !shellState.menuItems.length;
		badge.classList.toggle(
			'hidden',
			!shellState.badgeVisible
				|| !(shellAssets.verifiedBadge && shellAssets.verifiedBadge.url));
		badge.setAttribute(
			'aria-hidden',
			badge.classList.contains('hidden') ? 'true' : 'false');
		if (controls.menu.disabled) {
			closeMenu();
		}
	}

	function visibleButtons() {
		const main = shellState.buttons.main && shellState.buttons.main.visible
			? shellState.buttons.main
			: null;
		const secondary = shellState.buttons.secondary
			&& shellState.buttons.secondary.visible
			? shellState.buttons.secondary
			: null;
		const result = {
			layout: 'single',
			buttons: []
		};
		if (main && secondary) {
			const position = secondary.position || 'left';
			if (position === 'top') {
				result.layout = 'vertical';
				result.buttons = [secondary, main];
			} else if (position === 'bottom') {
				result.layout = 'vertical';
				result.buttons = [main, secondary];
			} else if (position === 'left') {
				result.layout = 'horizontal';
				result.buttons = [secondary, main];
			} else {
				result.layout = 'horizontal';
				result.buttons = [main, secondary];
			}
		} else if (main) {
			result.buttons = [main];
		} else if (secondary) {
			result.buttons = [secondary];
		}
		return result;
	}

	function requestButtonIcon(state) {
		if (!state
			|| !state.visible
			|| !state.iconCustomEmojiId
			|| state.iconResolvedGeneration === state.iconGeneration
			|| state.iconRequestGeneration === state.iconGeneration) {
			return;
		}
		state.iconRequestGeneration = state.iconGeneration;
		invokeShell('shell_request_button_icon', {
			name: state.name
		});
	}

	function updateFooter() {
		const visible = visibleButtons();
		const hasButtons = !!visible.buttons.length;
		disclosure.textContent = '';
		disclosure.classList.remove('visible');
		buttonsWrap.classList.toggle('visible', hasButtons);
		footer.classList.toggle('visible', hasButtons);
		root.style.setProperty(
			'--footer-gap',
			shellState.isFullscreen
				? '0px'
				: hasButtons
				? 'var(--footer-button-skip)'
				: 'var(--disclosure-skip)');
		scheduleViewport();
	}

	function renderButtons() {
		const visible = visibleButtons();
		buttons.textContent = '';
		buttons.dataset.layout = visible.layout;
		for (const state of visible.buttons) {
			const button = document.createElement('button');
			button.type = 'button';
			button.className = 'shell-button';
			button.disabled = !state.active;
			const buttonColor = state.color || '#40a7e3';
			button.style.background = buttonColor;
			button.style.color = state.textColor || '#ffffff';
			const rippleColor = buttonRippleForBackground(buttonColor);
			if (rippleColor) {
				button.style.setProperty('--button-ripple', rippleColor);
			}

			const icon = document.createElement('span');
			icon.className = 'button-icon';
			const iconReady = state.iconResolvedGeneration === state.iconGeneration;
			if (state.iconCustomEmojiId && state.iconUrl) {
				icon.classList.add('visible');
				icon.style.backgroundImage = 'url(' + state.iconUrl + ')';
			} else if (state.iconCustomEmojiId && !iconReady) {
				icon.classList.add('visible', 'pending');
				requestButtonIcon(state);
			}
			button.appendChild(icon);

			const label = document.createElement('span');
			label.className = 'button-label';
			label.textContent = state.text || '';
			button.appendChild(label);

			const spinner = document.createElement('span');
			spinner.className = 'button-spinner';
			if (state.progress) {
				spinner.classList.add('visible');
			}
			button.appendChild(spinner);

			button.addEventListener('click', function() {
				if (!button.disabled) {
					postToFrame(state.name + '_button_pressed', {});
				}
			});
			setupRipple(button);
			buttons.appendChild(button);
		}
		updateFooter();
	}

	function menuIconUrl(name) {
		const asset = shellAssets.icons && name ? shellAssets.icons[name] : null;
		return (asset && asset.url) ? asset.url : '';
	}

	function createMenuNode(item, className) {
		const clickable = !!item.id && item.enabled !== false;
		const node = document.createElement(clickable ? 'button' : 'div');
		node.className = className
			+ (item.attention ? ' attention' : '')
			+ (clickable ? '' : ' disabled');
		if (clickable) {
			node.type = 'button';
			setupRipple(node);
			node.addEventListener('click', function(event) {
				if (!shellState.blocked && event.isTrusted) {
					invokeShell('shell_menu_action', { id: item.id });
					closeMenu();
				}
			});
		}
		return node;
	}

	function createMenuCopy(item) {
		const copy = document.createElement('span');
		copy.className = 'menu-item-copy';
		const titleNode = document.createElement('span');
		titleNode.className = 'menu-item-title';
		titleNode.textContent = item.text || '';
		copy.appendChild(titleNode);
		if (item.subtitle) {
			const subtitleNode = document.createElement('span');
			subtitleNode.className = 'menu-item-subtitle';
			subtitleNode.textContent = item.subtitle;
			copy.appendChild(subtitleNode);
		}
		return copy;
	}

	function renderMenu() {
		menuList.textContent = '';
		for (const item of shellState.menuItems) {
			if (item.separator) {
				const separator = document.createElement('div');
				separator.className = 'menu-separator';
				menuList.appendChild(separator);
				continue;
			}
			if (Array.isArray(item.children) && item.children.length) {
				const group = document.createElement('div');
				group.className = 'menu-group';

				const header = document.createElement('div');
				header.className = 'menu-item menu-group-title';
				const headerIcon = document.createElement('span');
				headerIcon.className = 'menu-item-icon';
				const headerIconUrl = menuIconUrl(item.icon);
				if (headerIconUrl) {
					headerIcon.style.backgroundImage = 'url('
						+ headerIconUrl + ')';
				}
				header.appendChild(headerIcon);
				header.appendChild(createMenuCopy(item));
				group.appendChild(header);

				const children = document.createElement('div');
				children.className = 'menu-group-children';
				for (const child of item.children) {
					if (child.separator) {
						const separator = document.createElement('div');
						separator.className = 'menu-separator';
						children.appendChild(separator);
						continue;
					}
					const row = createMenuNode(child, 'menu-download');
					row.appendChild(createMenuCopy(child));
					if (child.actionLabel) {
						const action = document.createElement('span');
						action.className = 'menu-item-action';
						action.textContent = child.actionLabel;
						row.appendChild(action);
					}
					children.appendChild(row);
				}
				group.appendChild(children);
				menuList.appendChild(group);
				continue;
			}

			const row = createMenuNode(item, 'menu-item');
			const icon = document.createElement('span');
			icon.className = 'menu-item-icon';
			const iconUrl = menuIconUrl(item.icon);
			if (iconUrl) {
				icon.style.backgroundImage = 'url(' + iconUrl + ')';
			}
			row.appendChild(icon);
			row.appendChild(createMenuCopy(item));
			menuList.appendChild(row);
		}
		menu.classList.toggle(
			'visible',
			shellState.menuOpen
				&& shellState.menuVisible
				&& !!shellState.menuItems.length);
		menuBackdrop.classList.toggle(
			'visible',
			menu.classList.contains('visible'));
		controls.menu.classList.toggle(
			'active',
			menu.classList.contains('visible'));
	}
	function closeMenu() {
		if (!shellState.menuOpen) {
			return;
		}
		shellState.menuOpen = false;
		renderMenu();
	}

	function toggleMenu(event) {
		if (event && !event.isTrusted) {
			return;
		}
		if (shellState.blocked) {
			return;
		}
		if (shellState.menuOpen) {
			closeMenu();
			return;
		}
		invokeShell('shell_menu_request', {});
		shellState.menuOpen = true;
		renderMenu();
	}

	function parseFrameMessage(data) {
		if (typeof data === 'string') {
			try {
				return JSON.parse(data);
			} catch (e) {
				return null;
			}
		}
		return data && typeof data === 'object' && !Array.isArray(data)
			? data
			: null;
	}

	function addRipple(button, x, y) {
		if (!button || button.disabled) {
			return;
		}
		const rect = button.getBoundingClientRect();
		const ripple = document.createElement('span');
		ripple.className = 'ripple';
		const inner = document.createElement('span');
		inner.className = 'inner';
		const size = 2 * Math.hypot(
			Math.max(x, rect.width - x),
			Math.max(y, rect.height - y));
		inner.style.width = String(size) + 'px';
		inner.style.height = String(size) + 'px';
		inner.style.left = String(x - size / 2) + 'px';
		inner.style.top = String(y - size / 2) + 'px';
		ripple.appendChild(inner);
		button.appendChild(ripple);
	}

	function stopRipples(button) {
		const ripples = Array.prototype.slice.call(
			button.querySelectorAll('.ripple:not(.hiding)'));
		for (const ripple of ripples) {
			ripple.classList.add('hiding');
			window.setTimeout(function() {
				ripple.remove();
			}, 200);
		}
	}

	function setupRipple(button) {
		button.addEventListener('mousedown', function(event) {
			if (event.button !== 0) {
				return;
			}
			const rect = button.getBoundingClientRect();
			addRipple(button, event.clientX - rect.left, event.clientY - rect.top);
		});
		button.addEventListener('mouseup', function() {
			stopRipples(button);
		});
		button.addEventListener('mouseleave', function() {
			stopRipples(button);
		});
	}

	window.addEventListener('message', function(event) {
		if (!iframe
			|| !iframe.contentWindow
			|| event.source !== iframe.contentWindow) {
			return;
		}
		if (sameOrigin && (!frameOrigin || event.origin !== frameOrigin)) {
			return;
		}
		const message = parseFrameMessage(event.data);
		if (!message || typeof message.eventType !== 'string') {
			return;
		}
		if (message.eventType === 'iframe_ready') {
			reloadSupported = !!(message.eventData && message.eventData.reload_supported);
			return;
		} else if (message.eventType === 'iframe_will_reload') {
			if (reloadTimeout) {
				window.clearTimeout(reloadTimeout);
				reloadTimeout = null;
			}
			frameLoaded = false;
			return;
		}
		invokeWebApp(message.eventType, message.eventData, event.origin);
	});

	menuBackdrop.addEventListener('mousedown', closeMenu);
	blocker.addEventListener('click', function(event) {
		if (!event.isTrusted || !shellState.blocked) {
			return;
		}
		invokeShell('shell_close_layer', {});
		event.preventDefault();
		event.stopPropagation();
	});

	document.addEventListener('mousedown', function(event) {
		if (!shellState.menuOpen) {
			return;
		}
		const target = event.target;
		if (menu.contains(target)
			|| controls.menu.contains(target)
			|| menuBackdrop.contains(target)) {
			return;
		}
		closeMenu();
	});

	window.addEventListener('keydown', function(event) {
		if (event.key === 'Escape' && shellState.menuOpen) {
			event.preventDefault();
			closeMenu();
		}
	});

	window.addEventListener('resize', scheduleViewport);
	if (window.ResizeObserver) {
		resizeObserver = new window.ResizeObserver(scheduleViewport);
		resizeObserver.observe(frameShell);
		resizeObserver.observe(root);
	}

	controls.close.addEventListener('click', function(event) {
		if (event.isTrusted) {
			invokeShell('shell_close', {});
		}
	});
	controls.back.addEventListener('click', function() {
		postToFrame('back_button_pressed', {});
	});
	controls.menu.addEventListener('click', toggleMenu);
	setupRipple(controls.close);
	setupRipple(controls.back);
	setupRipple(controls.menu);
	header.addEventListener('selectstart', function(event) {
		event.preventDefault();
	});
	header.addEventListener('mousedown', beginShellMove);
	for (const handle of resizeHandles) {
		handle.addEventListener('mousedown', function(event) {
			beginShellResize(handle.getAttribute('data-resize-edge'), event);
		});
	}

	function createIframe(url) {
		closeMenu();
		if (reloadTimeout) {
			window.clearTimeout(reloadTimeout);
			reloadTimeout = null;
		}
		const generation = ++frameGeneration;
		pendingEvents.splice(0);
		const next = document.createElement('iframe');
		next.setAttribute('allow', 'clipboard-read; clipboard-write; fullscreen');
		next.referrerPolicy = 'no-referrer';
		next.addEventListener('load', function() {
			if (iframe !== next || generation !== frameGeneration) {
				return;
			}
			frameLoaded = true;
			flushPendingEvents();
			scheduleViewport();
		});
		frameLoaded = false;
		reloadSupported = false;
		if (iframe) {
			iframe.remove();
		}
		iframe = next;
		iframe.src = url || 'about:blank';
		frameWrap.appendChild(iframe);
	}

	function fallbackReloadFrame() {
		createIframe(frameUrl || 'about:blank');
	}

	function reloadFrame() {
		if (!iframe) {
			return;
		}
		if (reloadSupported && frameLoaded && iframe.contentWindow) {
			sendToFrame('reload_iframe', {}, frameGeneration);
			if (reloadTimeout) {
				window.clearTimeout(reloadTimeout);
			}
			reloadTimeout = window.setTimeout(function() {
				reloadTimeout = null;
				fallbackReloadFrame();
			}, 500);
			return;
		}
		fallbackReloadFrame();
	}

	function isNativeToken(token) {
		return !!shellToken && token === shellToken;
	}

	const api = {
		bootstrap: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			applyMetrics(data && data.metrics);
			applyColors(data && data.colors);
			applyChrome(data || {});
			shellState.bottomText = '';
			title.textContent = (data && data.title) || '';
			document.title = (data && data.title) || 'Telegram';
			sameOrigin = !!(data && data.sameOrigin);
			frameUrl = (data && data.url) || 'about:blank';
			frameOrigin = sameOrigin ? originFromUrl(frameUrl) : '';
			createIframe(frameUrl);
			renderButtons();
			renderMenu();
		},
		nativeEvent: function(eventType, eventData, token) {
			if (!isNativeToken(token) || typeof eventType !== 'string') {
				return;
			}
			if (eventType === 'fullscreen_changed' && eventData) {
				shellState.isFullscreen = !!eventData.is_fullscreen;
				root.classList.toggle('fullscreen', shellState.isFullscreen);
				updateFooter();
			}
			postToFrame(eventType, eventData || {});
		},
		setTitle: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			title.textContent = (data && data.title) || '';
			document.title = (data && data.title) || 'Telegram';
		},
		setChrome: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			applyChrome(data || {});
		},
		setColors: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			applyColors(data || {});
		},
		setAssets: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			applyAssets(data || {});
		},
		setMenu: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			shellState.menuItems = Array.isArray(data && data.items)
				? data.items
				: [];
			applyChrome({});
			renderMenu();
		},
		setBottomText: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			shellState.bottomText = '';
			updateFooter();
		},
		setButton: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			if (!data || !data.name) {
				return;
			}
			const previous = shellState.buttons[data.name] || {};
			const next = Object.assign({}, previous, data);
			if (next.iconGeneration !== previous.iconGeneration
				|| next.iconCustomEmojiId !== previous.iconCustomEmojiId) {
				next.iconRequestGeneration = '';
				next.iconResolvedGeneration = '';
				next.iconUrl = '';
			}
			if (!next.iconCustomEmojiId) {
				next.iconRequestGeneration = '';
				next.iconResolvedGeneration = next.iconGeneration || '';
				next.iconUrl = '';
			}
			shellState.buttons[data.name] = next;
			renderButtons();
		},
		setButtonIcon: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			if (!data || !data.name) {
				return;
			}
			const state = shellState.buttons[data.name];
			if (!state || state.iconGeneration !== (data.generation || '')) {
				return;
			}
			const icon = (data.icon && typeof data.icon === 'object')
				? data.icon
				: null;
			state.iconRequestGeneration = '';
			state.iconResolvedGeneration = state.iconGeneration;
			state.iconUrl = (icon && icon.url) ? icon.url : '';
			renderButtons();
		},
		setBlocked: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			shellState.blocked = !!(data && data.blocked);
			root.classList.toggle('blocked', shellState.blocked);
			if (shellState.blocked) {
				closeMenu();
			}
		},
		setProgress: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			root.classList.toggle('loading', !!(data && data.shown));
		},
		reloadFrame: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			reloadFrame();
		},
		sendViewport: function(data, token) {
			if (!isNativeToken(token)) {
				return;
			}
			scheduleViewport();
		}
	};
	Object.defineProperty(window, 'TelegramDesktopShell', {
		value: Object.freeze(api),
		configurable: false,
		writable: false
	});
})();

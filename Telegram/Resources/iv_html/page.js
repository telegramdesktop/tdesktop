var IV = {
  sendPostMessage: function(data) {
    try {
      window.parent.postMessage(JSON.stringify(data), window.parentOrigin);
    } catch(e) {}
  },
  frameClickHandler: function(e) {
    var target = e.target, href;
    do {
      if (target.tagName == 'SUMMARY') return;
      if (target.tagName == 'DETAILS') return;
      if (target.tagName == 'LABEL') return;
      if (target.tagName == 'AUDIO') return;
      if (target.tagName == 'A') break;
    } while (target = target.parentNode);
    if (target && target.hasAttribute('href')) {
      var base_loc = document.createElement('A');
      base_loc.href = window.currentUrl;
      if (base_loc.origin != target.origin ||
          base_loc.pathname != target.pathname ||
          base_loc.search != target.search) {
        IV.sendPostMessage({event: 'link_click', url: target.href});
      }
    }
    e.preventDefault();
  },
  postMessageHandler: function(event) {
    if (event.source !== window.parent ||
        event.origin != window.parentOrigin) {
      return;
    }
    try {
      var data = JSON.parse(event.data);
    } catch(e) {
      var data = {};
    }
  },
  slideshowSlide: function(el, next) {
    var dir = window.getComputedStyle(el, null).direction || 'ltr';
    var marginProp = dir == 'rtl' ? 'marginRight' : 'marginLeft';
    if (next) {
      var s = el.previousSibling.s;
      s.value = (+s.value + 1 == s.length) ? 0 : +s.value + 1;
      s.forEach(function(el){ el.checked && el.parentNode.scrollIntoView && el.parentNode.scrollIntoView({behavior: 'smooth', block: 'center', inline: 'center'}); });
      el.firstChild.style[marginProp] = (-100 * s.value) + '%';
    } else {
      el.form.nextSibling.firstChild.style[marginProp] = (-100 * el.value) + '%';
    }
    return false;
  },
  initPreBlocks: function() {
    if (!hljs) return;
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
  }
};

document.onclick = IV.frameClickHandler;
window.onmessage = IV.postMessageHandler;

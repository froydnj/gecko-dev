/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

Cu.import("resource://gre/modules/ExtensionUtils.jsm");
var {
  EventManager,
  DefaultWeakMap,
  runSafe,
} = ExtensionUtils;

// WeakMap[Extension -> PageAction]
var pageActionMap = new WeakMap();


// Handles URL bar icons, including the |page_action| manifest entry
// and associated API.
function PageAction(options, extension)
{
  this.extension = extension;
  this.id = makeWidgetId(extension.id) + "-page-action";

  this.tabManager = TabManager.for(extension);

  let title = extension.localize(options.default_title || "");
  let popup = extension.localize(options.default_popup || "");
  if (popup) {
    popup = extension.baseURI.resolve(popup);
  }

  this.defaults = {
    show: false,
    title: title,
    icon: IconDetails.normalize({ path: options.default_icon }, extension,
                                null, true),
    popup: popup && extension.baseURI.resolve(popup),
  };

  this.tabContext = new TabContext(tab => Object.create(this.defaults),
                                   extension);

  this.tabContext.on("location-change", this.handleLocationChange.bind(this));

  // WeakMap[ChromeWindow -> <xul:image>]
  this.buttons = new WeakMap();

  EventEmitter.decorate(this);
}

PageAction.prototype = {
  // Returns the value of the property |prop| for the given tab, where
  // |prop| is one of "show", "title", "icon", "popup".
  getProperty(tab, prop) {
    return this.tabContext.get(tab)[prop];
  },

  // Sets the value of the property |prop| for the given tab to the
  // given value, symmetrically to |getProperty|.
  //
  // If |tab| is currently selected, updates the page action button to
  // reflect the new value.
  setProperty(tab, prop, value) {
    this.tabContext.get(tab)[prop] = value;
    if (tab.selected) {
      this.updateButton(tab.ownerDocument.defaultView);
    }
  },

  // Updates the page action button in the given window to reflect the
  // properties of the currently selected tab:
  //
  // Updates "tooltiptext" and "aria-label" to match "title" property.
  // Updates "image" to match the "icon" property.
  // Shows or hides the icon, based on the "show" property.
  updateButton(window) {
    let tabData = this.tabContext.get(window.gBrowser.selectedTab);

    if (!(tabData.show || this.buttons.has(window))) {
      // Don't bother creating a button for a window until it actually
      // needs to be shown.
      return;
    }

    let button = this.getButton(window);

    if (tabData.show) {
      // Update the title and icon only if the button is visible.

      if (tabData.title) {
        button.setAttribute("tooltiptext", tabData.title);
        button.setAttribute("aria-label", tabData.title);
      } else {
        button.removeAttribute("tooltiptext");
        button.removeAttribute("aria-label");
      }

      let icon = IconDetails.getURL(tabData.icon, window, this.extension);
      button.setAttribute("src", icon);
    }

    button.hidden = !tabData.show;
  },

  // Create an |image| node and add it to the |urlbar-icons|
  // container in the given window.
  addButton(window) {
    let document = window.document;

    let button = document.createElement("image");
    button.id = this.id;
    button.setAttribute("class", "urlbar-icon");

    button.addEventListener("click", event => {
      if (event.button == 0) {
        this.handleClick(window);
      }
    });

    document.getElementById("urlbar-icons").appendChild(button);

    return button;
  },

  // Returns the page action button for the given window, creating it if
  // it doesn't already exist.
  getButton(window) {
    if (!this.buttons.has(window)) {
      let button = this.addButton(window);
      this.buttons.set(window, button);
    }

    return this.buttons.get(window);
  },

  // Handles a click event on the page action button for the given
  // window.
  // If the page action has a |popup| property, a panel is opened to
  // that URL. Otherwise, a "click" event is emitted, and dispatched to
  // the any click listeners in the add-on.
  handleClick(window) {
    let tab = window.gBrowser.selectedTab;
    let popup = this.tabContext.get(tab).popup;

    this.tabManager.addActiveTabPermission(tab);

    if (popup) {
      openPanel(this.getButton(window), popup, this.extension);
    } else {
      this.emit("click", tab);
    }
  },

  handleLocationChange(eventType, tab, fromBrowse) {
    if (fromBrowse) {
      this.tabContext.clear(tab);
    }
    this.updateButton(tab.ownerDocument.defaultView);
  },

  shutdown() {
    this.tabContext.shutdown();

    for (let window of WindowListManager.browserWindows()) {
      if (this.buttons.has(window)) {
        this.buttons.get(window).remove();
      }
    }
  },
};

PageAction.for = extension => {
  return pageActionMap.get(extension);
};


extensions.on("manifest_page_action", (type, directive, extension, manifest) => {
  let pageAction = new PageAction(manifest.page_action, extension);
  pageActionMap.set(extension, pageAction);
});

extensions.on("shutdown", (type, extension) => {
  if (pageActionMap.has(extension)) {
    pageActionMap.get(extension).shutdown();
    pageActionMap.delete(extension);
  }
});


extensions.registerAPI((extension, context) => {
  return {
    pageAction: {
      onClicked: new EventManager(context, "pageAction.onClicked", fire => {
        let listener = (evt, tab) => {
          fire(TabManager.convert(extension, tab));
        };
        let pageAction = PageAction.for(extension);

        pageAction.on("click", listener);
        return () => {
          pageAction.off("click", listener);
        };
      }).api(),

      show(tabId) {
        let tab = TabManager.getTab(tabId);
        PageAction.for(extension).setProperty(tab, "show", true);
      },

      hide(tabId) {
        let tab = TabManager.getTab(tabId);
        PageAction.for(extension).setProperty(tab, "show", false);
      },

      setTitle(details) {
        let tab = TabManager.getTab(details.tabId);
        PageAction.for(extension).setProperty(tab, "title", details.title);
      },

      getTitle(details, callback) {
        let tab = TabManager.getTab(details.tabId);
        let title = PageAction.for(extension).getProperty(tab, "title");
        runSafe(context, callback, title);
      },

      setIcon(details, callback) {
        let tab = TabManager.getTab(details.tabId);
        let icon = IconDetails.normalize(details, extension, context);
        PageAction.for(extension).setProperty(tab, "icon", icon);
      },

      setPopup(details) {
        let tab = TabManager.getTab(details.tabId);
        // Note: Chrome resolves arguments to setIcon relative to the calling
        // context, but resolves arguments to setPopup relative to the extension
        // root.
        // For internal consistency, we currently resolve both relative to the
        // calling context.
        let url = details.popup && context.uri.resolve(details.popup);
        PageAction.for(extension).setProperty(tab, "popup", url);
      },

      getPopup(details, callback) {
        let tab = TabManager.getTab(details.tabId);
        let popup = PageAction.for(extension).getProperty(tab, "popup");
        runSafe(context, callback, popup);
      },
    }
  };
});

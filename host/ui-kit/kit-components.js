/**
 * @file
 * Tag host UI kit: Preact rendering and the shared components.
 *
 * Load after preact-htm.js and kit.js. Adds to the global `Kit`:
 *
 * - `Kit.html`: htm bound to Preact, for templates written as tagged
 *   strings (`html\`<div class="x">${text}</div>\``), with no build step;
 * - `Kit.render(vnode, host)`, `Kit.h`, `Kit.Fragment` from Preact;
 * - `Kit.hooks`: Preact's hooks (useState, useEffect, useRef, ...);
 * - `Kit.ui`: the components below, each drawing the kit's own classes, so
 *   a page built from them has the kit's look without repeating its markup;
 * - `Kit.logModel()`: the text behind a LogPane, fed by the engine's `log`
 *   events.
 *
 * A component takes the classes and attributes it does not use itself
 * (`id`, `title`, `hidden`, ...) and passes them to its outer element.
 * Components hold no application state; a page renders them from the
 * engine's view state. See README.md.
 */
(function () {
  "use strict";

  const { h, render, Fragment } = window.preact;
  const hooks = window.preactHooks;
  const { useEffect, useLayoutEffect, useRef } = hooks;
  const html = window.htm.bind(h);

  /**
   * Joins class names, skipping empty and false ones.
   * @param {...(string|false|null|undefined)} names
   * @returns {string}
   */
  const cx = (...names) => names.filter(Boolean).join(" ");

  /** The log levels, most verbose first, as the engines name them. */
  const LEVELS = ["TRACE", "DEBUG", "INFO", "WARN", "ERROR"];

  /**
   * A push button (`kit-button`).
   * @param {Object} props
   * @param {string} [props.kind] "default" (the blue one) or "destructive".
   * @param {boolean} [props.small] The small size, as in a status row.
   * @param {Function} [props.onClick] Called on a click.
   * @returns {Object} A Preact element.
   */
  function Button({ kind, small, class: cls, children, ...rest }) {
    return html`<button type="button" class=${cx("kit-button", kind, small && "small", cls)} ...${rest}>${children}</button>`;
  }

  /**
   * A disclosure toggle (`kit-toolbutton`), such as "Log": an arrow that
   * turns when `pressed`.
   * @param {Object} props
   * @param {boolean} props.pressed Whether what it discloses is shown.
   * @param {Function} props.onClick Called on a click; the caller flips
   *     `pressed`.
   * @returns {Object} A Preact element.
   */
  function Disclosure({ pressed, class: cls, children, ...rest }) {
    return html`<button type="button" class=${cx("kit-toolbutton", cls)} aria-pressed=${String(!!pressed)} ...${rest}
      ><span class="kit-arrow">❯</span>${children}</button>`;
  }

  /**
   * A popup button (`kit-select`) showing `value`.
   *
   * The selection is set on every render, after the options, so it always
   * shows `value`, even when the user picked an entry the page then
   * refused. A `value` of null or undefined shows no selection.
   *
   * @param {Object} props
   * @param {Array<string|{value: string, text: string, disabled: boolean}>}
   *     props.options The entries; a string is both value and text.
   * @param {?string} props.value The entry shown.
   * @param {Function} [props.onChange] Called with the chosen entry's value.
   * @returns {Object} A Preact element.
   */
  function Select({ options, value, onChange, class: cls, ...rest }) {
    const ref = useRef(null);
    useLayoutEffect(() => {
      if (value === null || value === undefined) ref.current.selectedIndex = -1;
      else ref.current.value = value;
    });
    return html`<select ref=${ref} class=${cx("kit-select", cls)} ...${rest}
        onChange=${(ev) => onChange && onChange(ev.currentTarget.value)}>
      ${options.map((o) => (typeof o === "string"
        ? html`<option value=${o}>${o}</option>`
        : html`<option value=${o.value} disabled=${!!o.disabled}>${o.text}</option>`))}
    </select>`;
  }

  /**
   * A Qt macOS group box (`kit-group`): a small title above a rounded panel
   * holding the children.
   * @param {Object} props
   * @param {string} props.title The group's title.
   * @param {string} [props.titleTooltip] The title's own tooltip.
   * @returns {Object} A Preact element.
   */
  function Group({ title, titleTooltip, class: cls, children, ...rest }) {
    return html`<div class=${cx("kit-group", cls)} ...${rest}>
      <div class="kit-group-title" title=${titleTooltip}>${title}</div>
      <div class="kit-panel">${children}</div>
    </div>`;
  }

  /**
   * One row of a `kit-form`: a right-aligned label, then the value, as
   * selectable text. Children, when given, replace the value's text.
   * @param {Object} props
   * @param {string} props.label The label.
   * @param {*} [props.value] The value's text.
   * @param {boolean} [props.single] One line, cut with an ellipsis.
   * @returns {Object} A Preact fragment of the two grid cells.
   */
  function Row({ label, value, single, class: cls, children, ...rest }) {
    return html`<span class="kit-form-label">${label}</span><span
      class=${cx("kit-form-value", single && "single", "kit-selectable", cls)} ...${rest}>${children || value}</span>`;
  }

  /**
   * A progress bar (`kit-progress`).
   * @param {Object} props
   * @param {?number} props.fraction Done, from 0 to 1; null or undefined
   *     for an indeterminate (busy) bar.
   * @returns {Object} A Preact element.
   */
  function Progress({ fraction, class: cls, ...rest }) {
    const busy = fraction === null || fraction === undefined;
    const width = busy ? "" : 100 * Math.max(0, Math.min(1, fraction)) + "%";
    return html`<div class=${cx("kit-progress", busy && "busy", cls)} ...${rest}><div style=${{ width }}></div></div>`;
  }

  /**
   * Status rows (`kit-status-grid`): a coloured dot, a name, a text and
   * buttons per row. An empty text shows a dash.
   * @param {Object} props
   * @param {Array<{level: string, name: string, text: string, buttons: *}>}
   *     props.rows The rows, in order; `level` is ok, warn, fail or
   *     pending, `buttons` any Preact children.
   * @returns {Object} A Preact element.
   */
  function StatusGrid({ rows, class: cls, ...rest }) {
    return html`<div class=${cx("kit-status-grid", cls)} ...${rest}>
      ${rows.map((r) => html`<${Fragment} key=${r.name}>
        <span class=${cx("kit-dot", r.level)}>●</span>
        <span class="kit-status-name">${r.name}</span>
        <span class="kit-status-text kit-selectable" title=${r.text}>${r.text || "—"}</span>
        <div class="kit-status-buttons">${r.buttons}</div>
      <//>`)}
    </div>`;
  }

  /**
   * The text of a log, kept outside any component so it survives the pane
   * being hidden or redrawn, and so the engine's `log` listener can be
   * registered before the first render.
   * @returns {{append: Function, clear: Function, text: Function,
   *     subscribe: Function}} `append(line)` adds a line, `clear()` empties
   *     it, `text()` is all of it, and `subscribe(fn)` calls `fn` with each
   *     added chunk (null after a clear) and returns the unsubscribe call.
   */
  function logModel() {
    let text = "";
    const subscribers = new Set();
    return {
      append(line) {
        text += line + "\n";
        for (const f of subscribers) f(line + "\n");
      },
      clear() {
        text = "";
        for (const f of subscribers) f(null);
      },
      text: () => text,
      subscribe(f) {
        subscribers.add(f);
        return () => subscribers.delete(f);
      },
    };
  }

  /**
   * The log drawer (`kit-log`): a read-only text area showing `log`, and a
   * bar with Save Error Log, Clear (when `onClear` is given) and the log
   * level.
   *
   * The text area is written directly, not rendered, so a long log costs
   * nothing per render and keeps its selection. It follows new lines only
   * while scrolled to the end, so the user can read back.
   *
   * @param {Object} props
   * @param {Object} props.log A `Kit.logModel()`.
   * @param {string} props.level The level shown, one of TRACE..ERROR.
   * @param {Function} props.onLevel Called with a newly chosen level.
   * @param {Function} props.onSave Called by Save Error Log.
   * @param {Function} [props.onClear] Called by Clear; no button without it.
   * @returns {Object} A Preact element.
   */
  function LogPane({ log, level, onLevel, onSave, onClear, class: cls, ...rest }) {
    const ref = useRef(null);
    useEffect(() => {
      const area = ref.current;
      area.value = log.text();
      return log.subscribe((chunk) => {
        if (chunk === null) { area.value = ""; return; }
        const atEnd = area.scrollTop + area.clientHeight >= area.scrollHeight - 4;
        area.value += chunk;
        if (atEnd) area.scrollTop = area.scrollHeight;
      });
    }, [log]);
    return html`<div class=${cx("kit-log", cls)} ...${rest}>
      <textarea ref=${ref} readonly title="log of error messages" spellcheck="false"></textarea>
      <div class="kit-log-bar">
        <${Button} title="Save error log to file" onClick=${onSave}>Save Error Log<//>
        ${onClear && html`<${Button} title="Clear the error log" onClick=${onClear}>Clear<//>`}
        <span class="kit-spacer"></span>
        <label for="kit-log-level">Log Level</label>
        <${Select} id="kit-log-level" title="Set error logging level" options=${LEVELS} value=${level} onChange=${onLevel}/>
      </div>
    </div>`;
  }

  Object.assign(window.Kit, {
    html, h, render, Fragment, hooks, logModel, LEVELS,
    ui: { Button, Disclosure, Select, Group, Row, Progress, StatusGrid, LogPane },
  });
})();

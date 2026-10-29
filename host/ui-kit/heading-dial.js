/**
 * @file
 * A heading indicator, as sensorui's MyCompass.qml draws it: a card that
 * turns so the heading is under the top mark, and an aircraft that stays
 * put, nose up. Shared by Tag Calibrator's Orientation page and sensorViz's
 * compass panel; an SVG Preact component, so it scales with its box.
 *
 * Load after kit-components.js; defines `Kit.HeadingDial`.
 */
(function () {
  "use strict";

  const { html } = Kit;

  /** The dial's card: ticks every 5°, longer every 10°, figures every 30°. */
  const CARD = (() => {
    const ticks = [];
    for (let d = 0; d < 360; d += 5) {
      const long = d % 10 === 0;
      ticks.push(html`<line x1="0" y1=${long ? -86 : -93} x2="0" y2="-101" stroke="#fff"
        stroke-width=${long ? 2 : 1.2} transform=${`rotate(${d})`}/>`);
    }
    const figures = [];
    for (let d = 0; d < 360; d += 30) {
      figures.push(html`<text x="0" y="-66" fill="#fff" font-size="17" font-weight="700" text-anchor="middle"
        font-family="Helvetica, Arial, sans-serif" transform=${`rotate(${d})`}>${String(d / 10)}</text>`);
    }
    return [ticks, figures];
  })();

  /** The fixed bezel marks: a triangle every 45°, the top one the lubber line. */
  const MARKS = [0, 45, 90, 135, 180, 225, 270, 315].map((d) => html`<path d="M0,-103 L-6,-113 L6,-113 Z"
    fill="#fff" transform=${`rotate(${d})`}/>`);

  /**
   * A heading indicator: the card turns so the heading is under the top
   * mark; the aircraft stays put, nose up.
   * @param {Object} props
   * @param {?number} props.heading Degrees, 0 to 360, declination and
   *     battery direction already applied; null shows the card at 0.
   * @returns {Object} A Preact element.
   */
  function HeadingDial({ heading }) {
    return html`<svg class="dial" viewBox="-125 -125 250 250" role="img" aria-label="Heading indicator">
      <defs>
        <radialGradient id="bezel" cx="40%" cy="35%" r="70%">
          <stop offset="0" stop-color="#6b6f76"/><stop offset="1" stop-color="#1c1e22"/>
        </radialGradient>
      </defs>
      <circle r="123" fill="url(#bezel)"/>
      <circle r="114" fill="#0d0d0d"/>
      <g transform=${`rotate(${-(heading || 0)})`}>${CARD}</g>
      ${MARKS}
      <path d="M0,-46 L5,-36 L5,-10 L38,8 L38,16 L5,8 L5,28 L15,36 L15,42 L0,38 L-15,42 L-15,36 L-5,28 L-5,8 L-38,16 L-38,8 L-5,-10 L-5,-36 Z"
        fill="none" stroke="#fff" stroke-width="2" stroke-linejoin="round"/>
    </svg>`;
  }

  Kit.HeadingDial = HeadingDial;
})();

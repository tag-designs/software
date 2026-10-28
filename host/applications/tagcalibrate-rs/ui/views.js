/**
 * @file
 * Tag Calibrator's three graphics, as Preact components drawn from the
 * engine's numbers:
 *
 * - `SphereView`: the calibrated magnetometer samples on a sphere whose
 *   radius is the fitted field (three.js), qtcalibrate's magPlot;
 * - `TagView`: the tag model rotated to the tag's attitude (three.js), as
 *   sensorui's MyAttitude.qml builds it;
 * - `HeadingDial`: a heading indicator (SVG), as MyCompass.qml draws it.
 *
 * They compute nothing about the tag: points, quaternion and heading come
 * from the engine. What they keep is the camera, which the user turns by
 * dragging (and, for the tag, with Screen Direction). Load after
 * kit-components.js and three.min.js; defines one global, `CalViews`.
 */
(function () {
  "use strict";

  const { html } = Kit;
  const { useLayoutEffect, useRef } = Kit.hooks;
  const deg = Math.PI / 180;

  /**
   * A camera that orbits the origin with Z up, turned by dragging on its
   * canvas. Azimuth 0 looks along +Y from -Y; elevation is from the XY
   * plane, held short of the poles.
   * @param {HTMLCanvasElement} canvas Where drags are taken.
   * @param {THREE.Camera} camera The camera it places.
   * @param {number} distance From the origin, in scene units.
   * @param {number} azimuth Initial, in degrees.
   * @param {number} elevation Initial, in degrees.
   * @param {Function} changed Called after every move, to render.
   * @returns {{set: Function, place: Function, dispose: Function}}
   *     `set(az, el)` moves it (either may be null to keep it).
   */
  function orbit(canvas, camera, distance, azimuth, elevation, changed) {
    let az = azimuth, el = elevation, drag = null;
    function place() {
      const c = Math.cos(el * deg);
      camera.up.set(0, 0, 1);
      camera.position.set(distance * c * Math.sin(az * deg), -distance * c * Math.cos(az * deg), distance * Math.sin(el * deg));
      camera.lookAt(0, 0, 0);
    }
    const down = (ev) => { drag = { x: ev.clientX, y: ev.clientY }; canvas.setPointerCapture(ev.pointerId); };
    const move = (ev) => {
      if (!drag) return;
      az -= (ev.clientX - drag.x) * 0.5;
      el = Math.max(-89, Math.min(89, el + (ev.clientY - drag.y) * 0.5));
      drag = { x: ev.clientX, y: ev.clientY };
      place();
      changed();
    };
    const up = () => { drag = null; };
    canvas.addEventListener("pointerdown", down);
    canvas.addEventListener("pointermove", move);
    canvas.addEventListener("pointerup", up);
    canvas.addEventListener("pointercancel", up);
    place();
    return {
      set(a, e) { if (a !== null) az = a; if (e !== null) el = e; place(); changed(); },
      place,
      dispose() {
        canvas.removeEventListener("pointerdown", down);
        canvas.removeEventListener("pointermove", move);
        canvas.removeEventListener("pointerup", up);
        canvas.removeEventListener("pointercancel", up);
      },
    };
  }

  /**
   * A three.js renderer on a canvas, with a scene lit from above, that
   * renders on demand and follows the canvas's size (so a view in a hidden
   * tab draws correctly once shown).
   * @param {HTMLCanvasElement} canvas The canvas to draw on.
   * @param {THREE.Camera} camera Perspective or orthographic.
   * @param {Function} [fit] Called with (camera, width, height) on a
   *     resize, before the projection is updated.
   * @returns {{scene: THREE.Scene, render: Function, dispose: Function}}
   */
  function stage(canvas, camera, fit) {
    const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
    renderer.setPixelRatio(window.devicePixelRatio || 1);
    const scene = new THREE.Scene();
    scene.add(new THREE.HemisphereLight(0xffffff, 0x777777, 1.7));
    const sun = new THREE.DirectionalLight(0xffffff, 1.3);
    sun.position.set(200, -300, 400);
    scene.add(sun);
    const render = () => renderer.render(scene, camera);
    const observer = new ResizeObserver(() => {
      const w = canvas.clientWidth, h = canvas.clientHeight;
      if (!w || !h) return;
      renderer.setSize(w, h, false);
      fit(camera, w, h);
      camera.updateProjectionMatrix();
      render();
    });
    observer.observe(canvas);
    return { scene, render, dispose() { observer.disconnect(); renderer.dispose(); } };
  }

  /**
   * A line from the origin to `to`.
   * @param {number[]} to The end, [x, y, z].
   * @param {number} color 0xRRGGBB.
   * @returns {THREE.Line}
   */
  function axis(to, color) {
    const g = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3(...to)]);
    return new THREE.Line(g, new THREE.LineBasicMaterial({ color }));
  }

  /** The axis colours, x y z, as qtcalibrate's magPlot draws them. */
  const AXES = [[1, 0, 0, 0xd62728], [0, 1, 0, 0x2ca02c], [0, 0, 1, 0x1f5fd6]];

  /** magPlot's view height, in µT at zoom 1: 2.5 times its 50 µT reference field. */
  const VIEW_FIELD = 50;

  /**
   * The field circle's material: magPlot's radial gradient, from
   * rgba(175, 183, 196, 30) at the centre to rgba(120, 130, 150, 70) at the
   * rim. It stays see-through so samples on the far side show through it,
   * muted, which is what gives the plot its depth.
   * @returns {THREE.ShaderMaterial}
   */
  function fieldMaterial() {
    return new THREE.ShaderMaterial({
      transparent: true,
      depthWrite: false,
      vertexShader: `varying vec2 vPos;
        void main() { vPos = position.xy; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
      fragmentShader: `varying vec2 vPos;
        void main() {
          float t = clamp(length(vPos), 0.0, 1.0);
          gl_FragColor = mix(vec4(175.0, 183.0, 196.0, 30.0), vec4(120.0, 130.0, 150.0, 70.0), t) / 255.0;
        }`,
    });
  }

  /**
   * The calibrated samples on the field sphere, drawn as qtcalibrate's
   * magPlot draws them: a shaded, see-through circle of the field's radius
   * facing the viewer, the samples as dark magenta dots (pale on the far
   * side), the newest in yellow, the axes from the cyan
   * centre to the field in red, green and blue.
   *
   * It turns as magPlot does: each new sample is brought to the centre,
   * facing the viewer, by the shortest rotation that does it
   * (QQuaternion::rotationTo), so the sphere rolls under the samples as they
   * come in. A drag adds a turn of the user's own on top, which stays until
   * the samples are cleared; the wheel zooms.
   *
   * The projection is orthographic and its scale fixed, as magPlot's is, so
   * the circle's size on screen is the field: smaller at a 25 µT site than a
   * 60 µT one, and a point off it is visibly off it.
   *
   * @param {Object} props
   * @param {Object} props.sphere The state's `sphere`: `radius` (µT; the
   *     fitted field, or the engine's default before a fit), `points`
   *     (calibrated samples, flat x, y, z in µT), `latest` (index of the
   *     newest point, or -1 when there are none).
   * @returns {Object} A Preact element.
   */
  function SphereView({ sphere }) {
    const canvas = useRef(null);
    const view = useRef(null);
    // Built in a layout effect, declared first, so it exists before the
    // effects that update it run.
    useLayoutEffect(() => {
      const el = canvas.current;
      let zoom = 1, size = [1, 1];
      const camera = new THREE.OrthographicCamera(-1, 1, 1, -1, 1, 1000);
      camera.position.set(0, 0, 500);
      camera.lookAt(0, 0, 0);
      const frame = (c, w, h) => {
        // magPlot's scale: the shorter side spans 2.5 VIEW_FIELD / zoom.
        size = [w, h];
        const k = (VIEW_FIELD * 1.25) / (zoom * Math.min(w, h));
        c.left = -w * k; c.right = w * k; c.top = h * k; c.bottom = -h * k;
      };
      const st = stage(el, camera, frame);
      // The circle and its outline face the fixed camera; drawn after the
      // points, the circle covers those behind the centre and not those in
      // front.
      const disc = new THREE.Mesh(new THREE.CircleGeometry(1, 96), fieldMaterial());
      disc.renderOrder = 1;
      const rim = new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(
        Array.from({ length: 96 }, (_, i) => new THREE.Vector3(Math.cos(i * Math.PI / 48), Math.sin(i * Math.PI / 48), 0))),
        new THREE.LineBasicMaterial({ color: 0x8c8c8c, transparent: true, opacity: 0.8 }));
      rim.renderOrder = 1;
      const field = new THREE.Group();
      field.add(disc, rim);
      st.scene.add(field);
      // Everything that turns. magPlot's frame has y drawn downward and +z
      // away from the viewer, which is this one turned half about x.
      const content = new THREE.Group();
      st.scene.add(content);
      content.add(new THREE.Mesh(new THREE.SphereGeometry(2, 20, 12), new THREE.MeshPhongMaterial({ color: 0x00b8c0 })));
      // Each axis: a unit line scaled to the field, and a dot at its end.
      const axes = AXES.map(([x, y, z, color]) => {
        const line = axis([x, y, z], color);
        const tip = new THREE.Mesh(new THREE.SphereGeometry(0.5, 10, 8), new THREE.MeshBasicMaterial({ color }));
        content.add(line, tip);
        return { line, tip, to: new THREE.Vector3(x, y, z) };
      });
      const geometry = new THREE.BufferGeometry();
      content.add(new THREE.Points(geometry, new THREE.PointsMaterial({ vertexColors: true, size: 2.5, sizeAttenuation: false })));
      const latest = new THREE.Mesh(new THREE.SphereGeometry(1, 16, 10), new THREE.MeshBasicMaterial({ color: 0xffd400 }));
      content.add(latest);

      // magPlot's rotations, in its frame: `focus` brings the newest sample
      // to (0, 0, -1), toward the viewer; `user` is the drag's.
      const flip = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), Math.PI);
      const focus = new THREE.Quaternion();
      const user = new THREE.Quaternion();
      // Samples on the far side are drawn pale, so the sphere reads as
      // having a back. The circle tints them too, but at magPlot's 12-27%
      // that alone hardly shows on dots this small.
      const NEAR = new THREE.Color(0x8b008b), FAR = new THREE.Color(0xc9a6cf);
      const p = new THREE.Vector3();
      const render = () => {
        content.quaternion.copy(flip).multiply(user).multiply(focus);
        const pos = geometry.getAttribute("position"), col = geometry.getAttribute("color");
        if (pos && col) {
          for (let i = 0; i < pos.count; i++) {
            p.fromBufferAttribute(pos, i).applyQuaternion(content.quaternion);
            col.setXYZ(i, ...(p.z < 0 ? FAR : NEAR).toArray());
          }
          col.needsUpdate = true;
        }
        st.render();
      };

      // A drag turns about the screen's axes, up to 90° across the view,
      // as magPlot's right-button drag does (here with either button).
      let drag = null;
      const down = (ev) => {
        drag = { x: ev.clientX, y: ev.clientY, from: user.clone() };
        el.setPointerCapture(ev.pointerId);
      };
      const move = (ev) => {
        if (!drag) return;
        const roll = Math.max(-90, Math.min(90, 90 * (ev.clientX - drag.x) / size[0]));
        const pitch = Math.max(-90, Math.min(90, -90 * (ev.clientY - drag.y) / size[1]));
        // QQuaternion::fromEulerAngles(-pitch, -roll, 0): yaw about y, then pitch about x.
        const turn = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), -roll * deg)
          .multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), -pitch * deg));
        user.copy(turn).multiply(drag.from);
        render();
      };
      const up = () => { drag = null; };
      const wheel = (ev) => {
        ev.preventDefault();
        zoom = Math.max(0.3, Math.min(5, zoom * Math.exp(-ev.deltaY / 400)));
        frame(camera, ...size);
        camera.updateProjectionMatrix();
        render();
      };
      const noMenu = (ev) => { ev.preventDefault(); ev.stopPropagation(); };
      el.addEventListener("pointerdown", down);
      el.addEventListener("pointermove", move);
      el.addEventListener("pointerup", up);
      el.addEventListener("pointercancel", up);
      el.addEventListener("wheel", wheel, { passive: false });
      el.addEventListener("contextmenu", noMenu);
      view.current = { render, field, axes, geometry, latest, focus, user };
      return () => {
        for (const [t, f] of [["pointerdown", down], ["pointermove", move], ["pointerup", up], ["pointercancel", up],
          ["wheel", wheel], ["contextmenu", noMenu]]) el.removeEventListener(t, f);
        st.dispose();
      };
    }, []);
    useLayoutEffect(() => {
      const v = view.current;
      const r = sphere.radius;
      v.field.scale.setScalar(r);
      for (const a of v.axes) {
        a.line.scale.setScalar(r);
        a.tip.position.copy(a.to).multiplyScalar(r);
      }
      v.geometry.setAttribute("position", new THREE.Float32BufferAttribute(sphere.points, 3));
      v.geometry.setAttribute("color", new THREE.Float32BufferAttribute(new Float32Array(sphere.points.length), 3));
      v.geometry.computeBoundingSphere();
      const i = sphere.latest;
      v.latest.visible = i >= 0;
      if (i >= 0) {
        v.latest.position.fromArray(sphere.points, 3 * i);
        const p = v.latest.position.clone().normalize();
        if (p.lengthSq() > 0) v.focus.setFromUnitVectors(p, new THREE.Vector3(0, 0, -1));
      } else {
        // No samples (a new collection): magPlot's starting view.
        v.focus.identity();
        v.user.identity();
      }
      v.render();
    }, [sphere]);
    return html`<canvas ref=${canvas} class="view sphere-view"
      title="Calibrated magnetometer samples on a sphere of the fitted field; the newest is at the centre. Drag to turn it; scroll to zoom."></canvas>`;
  }

  /**
   * The tag model as MyAttitude.qml builds it -- a 100 × 200 × 10 board
   * with a coin cell 20 above it at y = +100 (battery forward) or -100 --
   * over a grid and the world axes, seen from the camera at distance 500,
   * 40 up. The engine's quaternion turns the model; dragging, or
   * `direction`, turns the camera about the vertical.
   *
   * @param {Object} props
   * @param {number[]} props.quaternion The model's rotation, [w, x, y, z].
   * @param {boolean} props.batteryForward Battery at +y.
   * @param {number} props.direction Screen direction, degrees: the
   *     camera's azimuth about Z.
   * @returns {Object} A Preact element.
   */
  function TagView({ quaternion, batteryForward, direction }) {
    const canvas = useRef(null);
    const view = useRef(null);
    // Built in a layout effect, declared first, so it exists before the
    // effects that update it run.
    useLayoutEffect(() => {
      const camera = new THREE.PerspectiveCamera(60, 1, 1, 5000);
      const st = stage(canvas.current, camera, (c, w, h) => { c.aspect = w / h; });
      const grid = new THREE.GridHelper(1000, 20, 0xbbbbbb, 0xdddddd);
      grid.rotation.x = Math.PI / 2;
      st.scene.add(grid);
      for (const [x, y, z, color] of AXES) st.scene.add(axis([x * 400, y * 400, z * 400], color));
      const tag = new THREE.Group();
      tag.add(new THREE.Mesh(new THREE.BoxGeometry(100, 200, 10),
        new THREE.MeshStandardMaterial({ color: 0x1f8f2f, roughness: 0.6 })));
      const battery = new THREE.Mesh(new THREE.CylinderGeometry(55, 55, 30, 48),
        new THREE.MeshPhongMaterial({ color: 0xd8dadd, shininess: 60 }));
      battery.rotation.x = Math.PI / 2;
      tag.add(battery);
      st.scene.add(tag);
      const cam = orbit(canvas.current, camera, Math.hypot(500, 40), 0, Math.atan2(40, 500) / deg, st.render);
      view.current = { st, tag, battery, cam };
      return () => { cam.dispose(); st.dispose(); };
    }, []);
    useLayoutEffect(() => { view.current.cam.set(direction, null); }, [direction]);
    useLayoutEffect(() => {
      const v = view.current;
      const [w, x, y, z] = quaternion;
      v.tag.quaternion.set(x, y, z, w);
      v.battery.position.set(0, batteryForward ? 100 : -100, 20);
      v.st.render();
    }, [quaternion, batteryForward]);
    return html`<canvas ref=${canvas} class="view tag-view"
      title="The tag's attitude. Drag to look from another side."></canvas>`;
  }

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

  window.CalViews = { SphereView, TagView, HeadingDial };
})();

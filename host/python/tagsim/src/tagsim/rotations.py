"""Quaternion and rotation helpers, vectorised over leading axes.

Quaternions are ``(w, x, y, z)``, Hamilton product, unit length. A quaternion
``q_ab`` rotates vectors from frame ``b`` into frame ``a``:
``v_a = q_ab * v_b * conj(q_ab)``, and its matrix is ``R_ab``.
"""

from __future__ import annotations

import numpy as np


def qmul(p: np.ndarray, q: np.ndarray) -> np.ndarray:
    """Hamilton product ``p * q``."""
    pw, px, py, pz = np.moveaxis(np.asarray(p), -1, 0)
    qw, qx, qy, qz = np.moveaxis(np.asarray(q), -1, 0)
    return np.stack([
        pw * qw - px * qx - py * qy - pz * qz,
        pw * qx + px * qw + py * qz - pz * qy,
        pw * qy - px * qz + py * qw + pz * qx,
        pw * qz + px * qy - py * qx + pz * qw,
    ], axis=-1)


def qconj(q: np.ndarray) -> np.ndarray:
    """Conjugate, the inverse of a unit quaternion."""
    q = np.asarray(q)
    return q * np.array([1.0, -1.0, -1.0, -1.0])


def qnormalize(q: np.ndarray) -> np.ndarray:
    """Scale to unit length."""
    q = np.asarray(q, dtype=np.float64)
    return q / np.linalg.norm(q, axis=-1, keepdims=True)


def qexp(v: np.ndarray) -> np.ndarray:
    """Quaternion of the rotation vector ``v`` (radians)."""
    v = np.asarray(v, dtype=np.float64)
    angle = np.linalg.norm(v, axis=-1, keepdims=True)
    half = 0.5 * angle
    # sin(a/2)/a, with its limit 1/2 at a = 0.
    k = np.where(angle > 1e-12, np.sin(half) / np.where(angle > 1e-12, angle, 1.0), 0.5)
    return np.concatenate([np.cos(half), k * v], axis=-1)


def qlog(q: np.ndarray) -> np.ndarray:
    """Rotation vector (radians) of ``q``, along the shorter way round."""
    q = np.asarray(q, dtype=np.float64)
    q = np.where(q[..., :1] < 0, -q, q)
    w = np.clip(q[..., :1], -1.0, 1.0)
    xyz = q[..., 1:]
    s = np.linalg.norm(xyz, axis=-1, keepdims=True)
    angle = 2.0 * np.arctan2(s, w)
    k = np.where(s > 1e-12, angle / np.where(s > 1e-12, s, 1.0), 2.0)
    return k * xyz


def qrotate(q: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Rotate ``v`` by ``q``: ``R(q) @ v``."""
    return np.einsum("...ij,...j->...i", qtodcm(q), v)


def qtodcm(q: np.ndarray) -> np.ndarray:
    """Rotation matrix of ``q``."""
    w, x, y, z = np.moveaxis(np.asarray(q, dtype=np.float64), -1, 0)
    return np.stack([
        np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)], -1),
        np.stack([2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)], -1),
        np.stack([2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)], -1),
    ], -2)


def dcmtoq(r: np.ndarray) -> np.ndarray:
    """Unit quaternion (w >= 0) of a rotation matrix."""
    r = np.asarray(r, dtype=np.float64)
    from scipy.spatial.transform import Rotation

    flat = r.reshape(-1, 3, 3)
    xyzw = Rotation.from_matrix(flat).as_quat()
    q = np.concatenate([xyzw[:, 3:], xyzw[:, :3]], axis=-1)
    q = np.where(q[:, :1] < 0, -q, q)
    return q.reshape(r.shape[:-2] + (4,))


def angle_between(p: np.ndarray, q: np.ndarray) -> np.ndarray:
    """Angle (radians) of the rotation taking ``p`` to ``q``."""
    d = np.abs(np.sum(np.asarray(p) * np.asarray(q), axis=-1))
    return 2.0 * np.arccos(np.clip(d, 0.0, 1.0))


def make_continuous(q: np.ndarray) -> np.ndarray:
    """Flip signs so each quaternion lies on the same side as its predecessor.

    ``q`` and ``-q`` are the same rotation; a spline through a sign flip
    would swing through a full turn.
    """
    q = np.array(q, dtype=np.float64)
    dots = np.sum(q[1:] * q[:-1], axis=-1)
    flips = np.concatenate([[1.0], np.cumprod(np.where(dots < 0, -1.0, 1.0))])
    return q * flips[:, None]


def slerp(t_src: np.ndarray, q_src: np.ndarray, t: np.ndarray) -> np.ndarray:
    """Spherical linear interpolation of a sign-continuous series at ``t``."""
    i = np.clip(np.searchsorted(t_src, t, side="right") - 1, 0, len(t_src) - 2)
    u = ((t - t_src[i]) / (t_src[i + 1] - t_src[i]))[:, None]
    rel = qlog(qmul(qconj(q_src[i]), q_src[i + 1]))
    return qmul(q_src[i], qexp(u * rel))

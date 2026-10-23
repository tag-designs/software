"""Which Config fields a tag supports, and the merge Restore and --merge use.

The Python counterpart of tagcore's ``ConfigShape``
(``host/libraries/tagcore/configshape.h``), with the same rules, so that
``examples/tag_start.py --merge`` behaves exactly like ``tag-start --merge``
and qtmonitor's Restore. See ``docs/shared/design/configuration-model.md``.

proto3 does not record whether a zero-valued scalar was set, so a decoded
``Config`` cannot say which fields a tag supports. Each tag's
``embedded/proto-c/<name>-proto-c/default-config.json`` does: it lists every
field the tag supports, zeros included. A *shape* is the set of those fields as
dotted .proto paths, such as ``"adxl362.freq"`` or ``"period"``.

    from tagclient import configshape
    file_shape = configshape.for_file(text)
    configshape.merge(config, parsed, file_shape)

The default configs are found in the checkout, as ``_protos`` finds
``proto/``; set ``TAGCLIENT_DEFAULT_CONFIG_DIR`` to an ``embedded/proto-c``
directory to use another.
"""

from __future__ import annotations

import functools
import json
import os
import warnings
from pathlib import Path

from google.protobuf.descriptor import FieldDescriptor

from ._protos import tagdata_pb2

Config = tagdata_pb2.Config
TagType = tagdata_pb2.TagType


def _default_config_dir() -> Path:
    env = os.environ.get("TAGCLIENT_DEFAULT_CONFIG_DIR")
    if env:
        return Path(env)
    # host/python/tagclient/src/tagclient/configshape.py -> repository root
    return Path(__file__).resolve().parents[5] / "embedded" / "proto-c"


def _is_repeated(field) -> bool:
    """Whether a field is repeated, on protobuf versions with and without
    ``FieldDescriptor.label`` (newer ones have only ``is_repeated``)."""
    is_repeated = getattr(field, "is_repeated", None)
    if is_repeated is not None:
        return bool(is_repeated)
    return field.label == FieldDescriptor.LABEL_REPEATED


def _find_field(descriptor, key: str):
    """Field of ``descriptor`` named ``key``, by .proto name or JSON name."""
    field = descriptor.fields_by_name.get(key)
    if field is not None:
        return field
    for candidate in descriptor.fields:
        if candidate.json_name == key:
            return candidate
    return None


def _walk(obj: dict, descriptor, prefix: str, paths: set[str]) -> None:
    """Record the path of every key in ``obj``, checked against ``descriptor``.

    Descends into objects and into the first element of arrays of objects.
    Raises ValueError at the first key that is not a field at its level.
    """
    for key, value in obj.items():
        if not prefix and key == "tag_type":
            continue
        field = _find_field(descriptor, key)
        if field is None:
            raise ValueError(f"no Config field {prefix}{key}")
        path = prefix + field.name
        paths.add(path)
        if field.cpp_type != FieldDescriptor.CPPTYPE_MESSAGE:
            continue
        child = None
        if isinstance(value, dict):
            child = value
        elif isinstance(value, list) and value and isinstance(value[0], dict):
            child = value[0]
        if child is not None:
            _walk(child, field.message_type, path + ".", paths)


def _parse(text: str) -> tuple[set[str], str]:
    """The shape of JSON text, and its ``tag_type`` name ("" if none)."""
    try:
        obj = json.loads(text)
    except json.JSONDecodeError as err:
        raise ValueError(f"not JSON: {err}") from err
    if not isinstance(obj, dict):
        raise ValueError("not a JSON object")
    paths: set[str] = set()
    _walk(obj, Config.DESCRIPTOR, "", paths)
    tag_type = obj.get("tag_type", "")
    return paths, tag_type if isinstance(tag_type, str) else ""


@functools.lru_cache(maxsize=1)
def _table() -> dict[int, frozenset[str]]:
    """Shapes of every tag type with a default config, built once."""
    shapes: dict[int, set[str]] = {}
    for path in sorted(_default_config_dir().glob("*-proto-c/default-config.json")):
        try:
            paths, name = _parse(path.read_text())
            tag_type = TagType.Value(name)
        except ValueError as err:
            warnings.warn(f"configshape: skipping {path}: {err}")
            continue
        if tag_type == TagType.TAG_UNSPECIFIED:
            warnings.warn(f"configshape: skipping {path}: no valid tag_type")
            continue
        # Two defaults with one tag type (PresTag, PresTagRaw) are unioned.
        shapes.setdefault(tag_type, set()).update(paths)
    return {tag_type: frozenset(paths) for tag_type, paths in shapes.items()}


def for_tag(tag_type: int) -> frozenset[str]:
    """Fields ``tag_type`` supports, from its default config(s).

    Empty for a tag type with no default config.
    """
    return _table().get(tag_type, frozenset())


def from_json(text: str) -> set[str]:
    """Fields a JSON Config text sets: its keys, zero-valued ones included.

    Raises ValueError when the text is not a JSON object or names a field
    Config does not have.
    """
    return _parse(text)[0]


def for_file(text: str) -> set[str]:
    """Fields a configuration file speaks for.

    qtmonitor's Save writes every field, padding those the tag lacks with
    zeros. When the file names a ``tag_type`` that has a default config, its
    shape is limited to that type's fields; otherwise the file is taken at its
    word. Raises ValueError as from_json() does.
    """
    paths, name = _parse(text)
    try:
        own = for_tag(TagType.Value(name)) if name else frozenset()
    except ValueError:
        own = frozenset()
    return paths & own if own else paths


def merge(target: Config, overlay: Config, overlay_shape: set[str],
          tag_shape: frozenset[str] | None = None) -> None:
    """Copy into ``target`` the fields in both shapes, from ``overlay``.

    ``tag_shape`` defaults to ``for_tag(target.tag_type)``. Scalars and enums
    are copied as they are, so a stated zero is copied; messages field by
    field; the repeated ``hibernate`` element by element up to the smaller
    count. ``tag_type`` is never copied, and a field outside the tag's shape is
    never added.
    """
    if tag_shape is None:
        tag_shape = for_tag(target.tag_type)
    skip_under = ""
    for path in sorted(tag_shape):
        if skip_under and path.startswith(skip_under):
            continue
        skip_under = ""
        if path not in overlay_shape:
            continue

        *parents, leaf = path.split(".")
        to, frm = target, overlay
        reachable = True
        for name in parents:
            field = to.DESCRIPTOR.fields_by_name[name]
            if _is_repeated(field):
                reachable = False
                break
            to, frm = getattr(to, name), getattr(frm, name)
        if not reachable:
            continue

        field = to.DESCRIPTOR.fields_by_name[leaf]
        if _is_repeated(field):
            if field.cpp_type == FieldDescriptor.CPPTYPE_MESSAGE:
                to_list, frm_list = getattr(to, leaf), getattr(frm, leaf)
                for i in range(min(len(to_list), len(frm_list))):
                    to_list[i].CopyFrom(frm_list[i])
            skip_under = path + "."
            continue
        if field.cpp_type == FieldDescriptor.CPPTYPE_MESSAGE:
            continue  # its fields are separate paths
        setattr(to, leaf, getattr(frm, leaf))

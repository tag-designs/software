#!/usr/bin/env python3
"""Generate a ChibiOS board.chcfg from a template and JSON customizations.

This tool supports two closely related workflows:

* Patch an existing board.chcfg when only GPIO pin definitions differ.
* Start from a stock ChibiOS processor XML file, customize board metadata,
  clocks, and pins, then hand the generated board.chcfg to fmpp.

The output intentionally preserves most template formatting. ElementTree is
used for validation and lookup, but final edits are applied to the original
text so generated board.chcfg diffs stay useful during review.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape


PIN_LOCATION_RE = re.compile(r"^(?:GPIO)?([A-Z])(?:PIN)?([0-9]|1[0-5])$")
PIN_METADATA_ATTRIBUTES = {"standby"}
STANDBY_VALUES = {"FLOAT", "PULLUP", "PULLDOWN"}
STANDBY_PORTS = ("A", "B", "C")


class ConfigError(ValueError):
    """Raised when the input configuration cannot be applied."""


class BoardConfig:
    """Parsed JSON customization data.

    element_text:
        XML element text replacements or additions, keyed by simple paths such
        as "board_name" or "configuration_settings/board_files_path".
    element_attributes:
        Attribute updates for non-pin XML elements, keyed by element path.
    remove_elements:
        Whole XML element blocks to delete from the template.
    pins:
        GPIO pin attribute updates normalized to (GPIOx, pin_number, attrs).
    """

    def __init__(self) -> None:
        self.element_text: dict[str, str] = {}
        self.element_attributes: dict[str, dict[str, str]] = {}
        self.remove_elements: list[str] = []
        self.pins: list[tuple[str, int, dict[str, str]]] = []


def normalize_location(value: str) -> tuple[str, int]:
    """Return (GPIOx, pin_number) from names such as PA5, GPIOA.pin5, or A5."""

    normalized = re.sub(r"[\s_.:/-]+", "", value.strip().upper())
    if normalized.startswith("P") and not normalized.startswith("PIN"):
        normalized = normalized[1:]

    match = PIN_LOCATION_RE.fullmatch(normalized)
    if match is None:
        raise ConfigError(
            f"invalid pin location {value!r}; use forms like PA5, GPIOA.pin5, or GPIOA:5"
        )

    return f"GPIO{match.group(1)}", int(match.group(2))


def normalize_port(value: str) -> str:
    port = value.strip().upper()
    if len(port) == 1:
        return f"GPIO{port}"
    if re.fullmatch(r"P[A-Z]", port):
        return f"GPIO{port[1]}"
    if re.fullmatch(r"GPIO[A-Z]", port):
        return port
    raise ConfigError(f"invalid GPIO port {value!r}; use forms like A, PA, or GPIOA")


def stringify_attributes(attributes: dict[str, Any], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for key, value in attributes.items():
        if not isinstance(value, (str, int, float, bool)) and value is not None:
            raise ConfigError(f"{label}: attribute {key!r} must be a scalar value")
        result[str(key)] = "" if value is None else str(value)
    return result


def normalize_standby_value(value: str, label: str) -> str:
    standby = value.strip().upper()
    if standby not in STANDBY_VALUES:
        allowed = ", ".join(sorted(STANDBY_VALUES))
        raise ConfigError(f"{label}: Standby must be one of {allowed}")
    return standby


def strip_pin_metadata(attributes: dict[str, str]) -> dict[str, str]:
    """Return attributes that should be written into ChibiOS board.chcfg."""

    return {
        key: value
        for key, value in attributes.items()
        if key.lower() not in PIN_METADATA_ATTRIBUTES
    }


def normalize_element_path(path: str) -> str:
    """Validate a slash-separated XML element path used by JSON customizations."""

    parts = [part.strip() for part in path.strip().split("/") if part.strip()]
    if not parts:
        raise ConfigError("element paths cannot be empty")
    for part in parts:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.:-]*", part):
            raise ConfigError(f"invalid XML element path {path!r}")
    return "/".join(parts)


def parse_pin_record(
    record: dict[str, Any], label: str
) -> tuple[str, int, dict[str, str]]:
    """Parse list-form pin records: {"port": "GPIOA", "pin": 5, ...}."""

    if "location" in record:
        port, pin = normalize_location(str(record["location"]))
    elif "port" in record and "pin" in record:
        port = normalize_port(str(record["port"]))
        try:
            pin = int(record["pin"])
        except (TypeError, ValueError) as exc:
            raise ConfigError(f"{label}: pin must be an integer from 0 to 15") from exc
        if pin < 0 or pin > 15:
            raise ConfigError(f"{label}: pin must be from 0 to 15")
    else:
        raise ConfigError(f"{label}: each pin record needs location or port/pin")

    attributes = {
        key: value for key, value in record.items() if key not in {"location", "port", "pin"}
    }
    return port, pin, stringify_attributes(attributes, label)


def iter_pin_records(pins_data: Any) -> list[tuple[str, int, dict[str, str]]]:
    """Accept the supported pin JSON shapes and normalize them to one list."""

    records: list[tuple[str, int, dict[str, str]]] = []

    if isinstance(pins_data, list):
        for index, item in enumerate(pins_data):
            if not isinstance(item, dict):
                raise ConfigError(f"pins[{index}] must be an object")
            records.append(parse_pin_record(item, f"pins[{index}]"))
        return records

    if not isinstance(pins_data, dict):
        raise ConfigError("pins must be an object or a list")

    for key, value in pins_data.items():
        if not isinstance(value, dict):
            raise ConfigError(f"pins.{key}: value must be an object")

        try:
            port, pin = normalize_location(str(key))
        except ConfigError:
            port = normalize_port(str(key))
            for pin_key, attributes in value.items():
                if not isinstance(attributes, dict):
                    raise ConfigError(f"pins.{key}.{pin_key}: value must be an object")
                pin_name = str(pin_key).strip().lower()
                if pin_name.startswith("pin"):
                    pin_name = pin_name[3:]
                try:
                    pin_number = int(pin_name)
                except ValueError as exc:
                    raise ConfigError(
                        f"pins.{key}.{pin_key}: nested pin key must be 0-15 or pin0-pin15"
                    ) from exc
                if pin_number < 0 or pin_number > 15:
                    raise ConfigError(f"pins.{key}.{pin_key}: pin must be from 0 to 15")
                records.append((
                    port,
                    pin_number,
                    stringify_attributes(attributes, f"pins.{key}.{pin_key}"),
                ))
            continue

        records.append((port, pin, stringify_attributes(value, f"pins.{key}")))

    return records


def load_board_config(path: Path) -> BoardConfig:
    """Load JSON and split board-level XML edits from GPIO pin edits."""

    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    config = BoardConfig()
    if isinstance(data, dict) and "pins" in data:
        for key in ("board_name", "board_id"):
            if key in data:
                config.element_text[key] = str(data[key])
        metadata = data.get("metadata")
        if isinstance(metadata, dict):
            for key in ("board_name", "board_id"):
                if key in metadata:
                    config.element_text[key] = str(metadata[key])

        elements = data.get("elements", {})
        if not isinstance(elements, dict):
            raise ConfigError("elements must be an object")
        for path_key, value in elements.items():
            if not isinstance(value, (str, int, float, bool)) and value is not None:
                raise ConfigError(f"elements.{path_key}: value must be a scalar")
            config.element_text[normalize_element_path(str(path_key))] = (
                "" if value is None else str(value)
            )

        attributes = data.get("attributes", {})
        if not isinstance(attributes, dict):
            raise ConfigError("attributes must be an object")
        for path_key, values in attributes.items():
            if not isinstance(values, dict):
                raise ConfigError(f"attributes.{path_key}: value must be an object")
            config.element_attributes[normalize_element_path(str(path_key))] = (
                stringify_attributes(values, f"attributes.{path_key}")
            )

        remove_elements = data.get("remove_elements", [])
        if not isinstance(remove_elements, list):
            raise ConfigError("remove_elements must be a list")
        config.remove_elements = [
            normalize_element_path(str(path_key)) for path_key in remove_elements
        ]
        pins_data = data["pins"]
    else:
        pins_data = data

    config.pins = iter_pin_records(pins_data)
    return config


def parse_template(path: Path) -> ET.ElementTree:
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    return ET.parse(path, parser=parser)


def find_pin(root: ET.Element, port: str, pin: int) -> ET.Element:
    ports = root.find("ports")
    if ports is None:
        raise ConfigError("template does not contain a <ports> section")

    port_element = ports.find(port)
    if port_element is None:
        raise ConfigError(f"template does not contain <{port}>")

    pin_element = port_element.find(f"pin{pin}")
    if pin_element is None:
        raise ConfigError(f"template does not contain <{port}>/<pin{pin}>")

    return pin_element


def validate_element_path(root: ET.Element, path: str, *, allow_missing_leaf: bool) -> None:
    element = root.find(path)
    if element is not None:
        return

    if not allow_missing_leaf:
        raise ConfigError(f"template does not contain <{path}>")

    parts = path.split("/")
    if len(parts) == 1:
        raise ConfigError(f"template does not contain <{path}>")

    parent = root.find("/".join(parts[:-1]))
    if parent is None:
        raise ConfigError(f"template does not contain parent for <{path}>")


def validate_xml_customizations(root: ET.Element, config: BoardConfig) -> None:
    """Fail early if requested XML paths cannot be applied to this template."""

    for element_name in config.element_text:
        validate_element_path(root, element_name, allow_missing_leaf=True)
    for element_name in config.element_attributes:
        validate_element_path(root, element_name, allow_missing_leaf=False)
    for element_name in config.remove_elements:
        validate_element_path(root, element_name, allow_missing_leaf=False)


def resolve_pins(
    root: ET.Element,
    records: list[tuple[str, int, dict[str, str]]],
    *,
    allow_new_attributes: bool,
) -> list[tuple[str, int, dict[str, str]]]:
    """Check pin existence and canonicalize attribute names from the template."""

    seen: set[tuple[str, int]] = set()
    resolved: list[tuple[str, int, dict[str, str]]] = []

    for port, pin, attributes in records:
        location = (port, pin)
        if location in seen:
            raise ConfigError(f"pin {port}.pin{pin} is defined more than once")
        seen.add(location)

        pin_element = find_pin(root, port, pin)
        attribute_names = {name.lower(): name for name in pin_element.attrib}
        resolved_attributes: dict[str, str] = {}
        for key, value in strip_pin_metadata(attributes).items():
            attribute_name = attribute_names.get(key.lower())
            if attribute_name is None:
                if not allow_new_attributes:
                    allowed = ", ".join(pin_element.attrib.keys())
                    raise ConfigError(
                        f"{port}.pin{pin}: unknown attribute {key!r}; allowed attributes: {allowed}"
                    )
                attribute_name = key
            resolved_attributes[attribute_name] = value
        resolved.append((port, pin, resolved_attributes))

    return resolved


def validate_standby_pin(port: str, pin: int, attributes: dict[str, str]) -> str:
    label = f"{port}.pin{pin}"
    standby = normalize_standby_value(attributes.get("Standby", "FLOAT"), label)
    if standby == "FLOAT":
        return standby

    port_letter = port[4:] if port.startswith("GPIO") else port
    if port_letter not in STANDBY_PORTS:
        raise ConfigError(f"{label}: Standby only supports GPIOA, GPIOB, and GPIOC")

    pin_id = attributes.get("ID", "").strip()
    if not pin_id:
        raise ConfigError(f"{label}: Standby={standby} requires a non-empty ID")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", pin_id):
        raise ConfigError(f"{label}: ID {pin_id!r} cannot be used as a C identifier")

    if port == "GPIOA" and standby == "PULLUP" and pin == 14:
        raise ConfigError(f"{label}: PA14 cannot be configured as a standby pull-up")
    if port == "GPIOA" and standby == "PULLDOWN" and pin in {13, 15}:
        raise ConfigError(f"{label}: PA{pin} cannot be configured as a standby pull-down")

    return standby


def render_standby_mask(pin_ids: list[str]) -> str:
    if not pin_ids:
        return "0U"
    terms = [f"PAL_PORT_BIT(PAL_PAD(LINE_{pin_id}))" for pin_id in pin_ids]
    expression = " | \\\n  ".join(terms)
    return f"({expression})"


def render_standby_header(records: list[tuple[str, int, dict[str, str]]]) -> str:
    masks: dict[tuple[str, str], list[str]] = {
        (kind, port): [] for kind in ("PULLUP", "PULLDWN") for port in STANDBY_PORTS
    }

    for port, pin, attributes in records:
        standby = validate_standby_pin(port, pin, attributes)
        if standby == "FLOAT":
            continue
        port_letter = port[4:] if port.startswith("GPIO") else port
        mask_kind = "PULLUP" if standby == "PULLUP" else "PULLDWN"
        masks[(mask_kind, port_letter)].append(attributes["ID"].strip())

    lines = [
        "/*",
        " * Generated from board-customizations.json. Do not edit by hand.",
        " */",
        "",
        "#ifndef BOARD_STANDBY_H",
        "#define BOARD_STANDBY_H",
        "",
        "#define BOARD_STANDBY_HAS_CONFIG 1",
        "",
    ]

    for port in STANDBY_PORTS:
        pullup = masks[("PULLUP", port)]
        pulldown = masks[("PULLDWN", port)]
        lines.append(f"#define HAS_PULLUP{port} {1 if pullup else 0}")
        lines.append(f"#define HAS_PULLDWN{port} {1 if pulldown else 0}")
        lines.append(f"#define PULLUP{port} {render_standby_mask(pullup)}")
        lines.append(f"#define PULLDWN{port} {render_standby_mask(pulldown)}")
        if port != STANDBY_PORTS[-1]:
            lines.append("")

    lines.extend(["", "#endif"])
    return "\n".join(lines)


def find_element_span(text: str, path: str) -> tuple[int, int] | None:
    """Find the byte span of a simple XML element path in the original text.

    The board templates use regular, non-namespaced element names for the
    customized areas. Keeping this text-level lookup narrow lets the script
    preserve ChibiOS' hand-written XML formatting while ElementTree still
    validates the final result.
    """

    scope_start = 0
    scope_end = len(text)

    for name in path.split("/"):
        scope = text[scope_start:scope_end]
        pattern = re.compile(
            rf"<{re.escape(name)}\b[^>]*(?:/>|>.*?</{re.escape(name)}>)",
            re.DOTALL,
        )
        match = pattern.search(scope)
        if match is None:
            return None
        scope_start += match.start()
        scope_end = scope_start + len(match.group(0))

    return scope_start, scope_end


def xml_escape_attribute(value: str) -> str:
    return escape(value, {'"': "&quot;", "'": "&apos;"})


def xml_escape_text(value: str) -> str:
    return escape(value)


def remove_element_text(text: str, path: str) -> str:
    """Remove an element block, including its otherwise-empty source line."""

    span = find_element_span(text, path)
    if span is None:
        raise ConfigError(f"template text does not contain <{path}>")

    start, end = span
    line_start = text.rfind("\n", 0, start) + 1
    if text[line_start:start].strip() == "":
        start = line_start
    if end < len(text) and text[end] == "\n":
        end += 1
    return text[:start] + text[end:]


def apply_element_text(text: str, path: str, value: str) -> str:
    """Replace existing element text or append a missing child to its parent."""

    span = find_element_span(text, path)
    escaped_value = xml_escape_text(value)

    if span is not None:
        start, end = span
        block = text[start:end]
        element_name = path.split("/")[-1]
        pattern = re.compile(
            rf"(<{re.escape(element_name)}\b[^>]*>)(.*?)(</{re.escape(element_name)}>)",
            re.DOTALL,
        )
        block, count = pattern.subn(
            lambda match: f"{match.group(1)}{escaped_value}{match.group(3)}",
            block,
            count=1,
        )
        if count != 1:
            raise ConfigError(f"cannot replace text for self-closing <{path}>")
        return text[:start] + block + text[end:]

    parent_path, element_name = path.rsplit("/", 1)
    parent_span = find_element_span(text, parent_path)
    if parent_span is None:
        raise ConfigError(f"template text does not contain parent for <{path}>")

    parent_start, parent_end = parent_span
    parent_block = text[parent_start:parent_end]
    close_match = re.search(rf"</{re.escape(parent_path.split('/')[-1])}>$", parent_block)
    if close_match is None:
        raise ConfigError(f"cannot add child to self-closing <{parent_path}>")

    body = parent_block[: close_match.start()]
    trailing_space = re.search(r"(\n[ \t]*)$", body)
    close_indent = trailing_space.group(1) if trailing_space else "\n"
    child_indent = f"{close_indent}  "
    child = f"{child_indent}<{element_name}>{escaped_value}</{element_name}>"
    replacement = body.rstrip() + child + close_indent + parent_block[close_match.start():]
    return text[:parent_start] + replacement + text[parent_end:]


def replace_element_attribute(block: str, attribute: str, value: str) -> tuple[str, int]:
    pattern = re.compile(rf"(\b{re.escape(attribute)}\s*=\s*)([\"'])(.*?)(\2)")
    escaped_value = xml_escape_attribute(value)
    return pattern.subn(
        lambda match: f"{match.group(1)}{match.group(2)}{escaped_value}{match.group(4)}",
        block,
        count=1,
    )


def apply_element_attributes(text: str, path: str, attributes: dict[str, str]) -> str:
    """Update or append attributes on a non-pin element opening tag."""

    span = find_element_span(text, path)
    if span is None:
        raise ConfigError(f"template text does not contain <{path}>")

    start, end = span
    block = text[start:end]
    open_match = re.match(r"<[A-Za-z_][A-Za-z0-9_.:-]*\b[^>]*", block, re.DOTALL)
    if open_match is None:
        raise ConfigError(f"cannot find opening tag for <{path}>")

    opening = open_match.group(0)
    for attribute, value in attributes.items():
        opening, count = replace_element_attribute(opening, attribute, value)
        if count == 0:
            opening = f'{opening} {attribute}="{xml_escape_attribute(value)}"'
    block = opening + block[open_match.end():]
    return text[:start] + block + text[end:]


def replace_pin_attribute(block: str, attribute: str, value: str) -> tuple[str, bool]:
    pattern = re.compile(rf"(\b{re.escape(attribute)}\s*=\s*)([\"'])(.*?)(\2)")
    escaped_value = xml_escape_attribute(value)
    return pattern.subn(
        lambda match: f"{match.group(1)}{match.group(2)}{escaped_value}{match.group(4)}",
        block,
        count=1,
    )


def add_pin_attribute(block: str, attribute: str, value: str) -> str:
    indent_match = re.search(r"\n([ \t]+)[A-Za-z_][A-Za-z0-9_:-]*\s*=", block)
    indent = indent_match.group(1) if indent_match else "  "
    escaped_value = xml_escape_attribute(value)
    return re.sub(
        r"\s*/>$",
        f'\n{indent}{attribute}="{escaped_value}" />',
        block,
        count=1,
    )


def apply_pin_text(text: str, port: str, pin: int, attributes: dict[str, str]) -> str:
    """Apply attribute updates to one <GPIOx>/<pinN /> block in the text."""

    port_pattern = re.compile(
        rf"(?P<open><{re.escape(port)}\b[^>]*>)(?P<body>.*?)(?P<close></{re.escape(port)}>)",
        re.DOTALL,
    )
    port_match = port_pattern.search(text)
    if port_match is None:
        raise ConfigError(f"template text does not contain <{port}>")

    pin_pattern = re.compile(rf"<pin{pin}\b.*?/>", re.DOTALL)
    body = port_match.group("body")
    pin_match = pin_pattern.search(body)
    if pin_match is None:
        raise ConfigError(f"template text does not contain <{port}>/<pin{pin}>")

    block = pin_match.group(0)
    for attribute, value in attributes.items():
        block, count = replace_pin_attribute(block, attribute, value)
        if count == 0:
            block = add_pin_attribute(block, attribute, value)

    new_body = body[: pin_match.start()] + block + body[pin_match.end() :]
    return text[: port_match.start("body")] + new_body + text[port_match.end("body") :]


def render_config(
    template_text: str,
    config: BoardConfig,
    pins: list[tuple[str, int, dict[str, str]]],
) -> str:
    """Apply all text patches and parse the result once as final XML validation."""

    text = template_text
    for path in config.remove_elements:
        text = remove_element_text(text, path)
    for path, value in config.element_text.items():
        text = apply_element_text(text, path, value)
    for path, attributes in config.element_attributes.items():
        text = apply_element_attributes(text, path, attributes)
    for port, pin, attributes in pins:
        text = apply_pin_text(text, port, pin, attributes)

    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    ET.fromstring(text, parser=parser)
    return text


def write_text(text: str, output: Path | None) -> None:
    if not text.endswith("\n"):
        text = f"{text}\n"

    if output is None:
        sys.stdout.write(text)
        return

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate a ChibiOS board.chcfg from JSON board customizations."
    )
    parser.add_argument(
        "--template",
        required=True,
        type=Path,
        help="Template board.chcfg to read.",
    )
    parser.add_argument(
        "--pins",
        "--customizations",
        dest="pins",
        required=True,
        type=Path,
        help="JSON file containing board customizations and pin definitions.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Destination board.chcfg. Omit to write to stdout.",
    )
    parser.add_argument(
        "--standby-header",
        type=Path,
        help="Optional destination for generated standby pull masks.",
    )
    parser.add_argument(
        "--board-name",
        help="Override <board_name> in the generated file.",
    )
    parser.add_argument(
        "--board-id",
        help="Override <board_id> in the generated file.",
    )
    parser.add_argument(
        "--allow-new-attributes",
        action="store_true",
        help="Allow pin attributes that are not present on the template pin.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)

    try:
        config = load_board_config(args.pins)
        if args.board_name is not None:
            config.element_text["board_name"] = args.board_name
        if args.board_id is not None:
            config.element_text["board_id"] = args.board_id

        template_text = args.template.read_text(encoding="utf-8")
        tree = parse_template(args.template)
        root = tree.getroot()
        validate_xml_customizations(root, config)
        pins = resolve_pins(root, config.pins, allow_new_attributes=args.allow_new_attributes)
        output_text = render_config(template_text, config, pins)
        write_text(output_text, args.output)
        if args.standby_header is not None:
            write_text(render_standby_header(config.pins), args.standby_header)
    except (ConfigError, ET.ParseError, json.JSONDecodeError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.output is not None:
        print(f"wrote {args.output} with {len(pins)} pin update(s)", file=sys.stderr)
    if args.standby_header is not None:
        print(f"wrote {args.standby_header}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Strict, shared configuration boundary for editor, JSON files and demo."""
from __future__ import annotations

import copy
import json

DEFAULT_CONFIG = {
    # UI/protocol use 1..16; channel 2 encodes MIDI wire channel ID 1.
    'channel': 2, 'notes': list(range(60, 72)), 'gains': [100] * 12,
    'velocity': [20, 500, 127, 16], 'gate': 200, 'volume': 100,
    'transpose': 0, 'fine': 0, 'cutoff': 127, 'resonance': 0,
    'polarity': 1, 'debounce': 2, 'enc_edges': 2, 'enc_reverse': 0,
    'route': 'auto', 'pins': [[-1, -1, -1] for _ in range(4)],
}

# B profile, mirrored from b_pinmap.c; this is a whitelist, never a wiring guess.
ENCODER_GPIO_WHITELIST = frozenset((1, 2, 8, 12, 13, 14, 21, 38, 39, 40, 41, 42, 47))
RANGES = {
    'channel': (1, 16), 'gate': (10, 5000), 'volume': (0, 127),
    'transpose': (-24, 24), 'fine': (-100, 100), 'cutoff': (0, 127),
    'resonance': (0, 127), 'polarity': (0, 1), 'debounce': (1, 50),
    'enc_reverse': (0, 15),
}


class ConfigError(ValueError):
    """A complete config failed the public v1 contract."""


def _integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise ConfigError(f'{name} 必须是 {low}..{high} 的整数')


def validate_config(config: dict) -> dict:
    """Return an independent validated config; reject unknown/missing keys."""
    if not isinstance(config, dict) or set(config) != set(DEFAULT_CONFIG):
        actual = set(config) if isinstance(config, dict) else set()
        raise ConfigError(f'配置字段不完整或未知：缺少 {sorted(set(DEFAULT_CONFIG) - actual)}；'
                          f'未知 {sorted(actual - set(DEFAULT_CONFIG))}')
    for key, (low, high) in RANGES.items():
        _integer(config[key], low, high, key)
    if type(config['enc_edges']) is not int or config['enc_edges'] not in (2, 4):
        raise ConfigError('enc_edges 只能为 2 或 4')
    if config['route'] not in ('auto', 'usb', 'ble', 'both'):
        raise ConfigError('route 只能为 auto / usb / ble / both')
    for key, size, low, high in (('notes', 12, 0, 127), ('gains', 12, 25, 200),
                                 ('velocity', 4, 1, 60000)):
        values = config[key]
        if not isinstance(values, list) or len(values) != size:
            raise ConfigError(f'{key} 必须包含 {size} 个整数')
        for value in values:
            _integer(value, low, high, key)
    if len(set(config['notes'])) != 12:
        raise ConfigError('12 路 MIDI note 必须互不重复')
    lo, hi, start, end = config['velocity']
    if lo >= hi or not (1 <= start <= 127 and 1 <= end <= 127):
        raise ConfigError('曲线须 1 ≤ min_ms < max_ms ≤ 60000，力度端点 1..127')
    pins = config['pins']
    if not isinstance(pins, list) or len(pins) != 4:
        raise ConfigError('pins 必须为 4 × 3')
    used = set()
    for index, row in enumerate(pins):
        if not isinstance(row, list) or len(row) != 3 or any(type(p) is not int for p in row):
            raise ConfigError('pins 必须为 4 × 3 整数')
        if row == [-1, -1, -1]:
            continue
        for pin in row:
            if pin not in ENCODER_GPIO_WHITELIST or pin in used:
                raise ConfigError(f'ENC{index + 1} GPIO 不在 B 板白名单、绑定不完整或重复')
            used.add(pin)
    return copy.deepcopy(config)


def effective_notes(config: dict) -> list[int]:
    return [n + config['transpose'] if 0 <= n + config['transpose'] <= 127 else -1
            for n in config['notes']]


def velocity_for(config: dict, duration_ms: int, channel_index: int = 0) -> int:
    """The firmware curve: round the global curve, then gain; clamp 1..127."""
    lo, hi, start, end = config['velocity']
    duration = max(lo, min(hi, duration_ms))
    span = hi - lo
    numerator = start * (hi - duration) + end * (duration - lo)
    base = (numerator + span // 2) // span
    return max(1, min(127, (base * config['gains'][channel_index] + 50) // 100))


def apply_arguments(config: dict, keys=None) -> str:
    validate_config(config)
    selected = list(DEFAULT_CONFIG) if keys is None else list(keys)
    if len(set(selected)) != len(selected) or any(key not in DEFAULT_CONFIG for key in selected):
        raise ConfigError('apply 字段未知或重复')
    result = []
    for key in selected:
        value = config[key]
        if key == 'pins':
            value = [pin for row in value for pin in row]
        value = ','.join(str(v) for v in value) if isinstance(value, list) else str(value)
        result.append(f'{key}={value}')
    return ' '.join(result)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'重复 JSON 字段：{key}')
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f'非法 JSON 数值：{value}')


def import_config(text: str) -> dict:
    if len(text.encode('utf-8')) > 65536:
        raise ConfigError('配置文件超过 64 KiB')
    try:
        obj = json.loads(text, object_pairs_hook=unique_object, parse_constant=reject_constant)
        return validate_config(obj)
    except (ValueError, TypeError, RecursionError) as exc:
        raise ConfigError(str(exc)) from exc


def parse_apply_arguments(arguments: str, current: dict) -> dict:
    """Demo device side of the same atomic patch used by the firmware."""
    result = copy.deepcopy(current)
    seen = set()
    if not arguments.strip():
        raise ConfigError('apply 至少需要一个字段')
    try:
        for item in arguments.split():
            key, value = item.split('=', 1)
            if key not in DEFAULT_CONFIG or key in seen or not value:
                raise ConfigError('apply 字段未知、重复或缺值')
            seen.add(key)
            if key == 'route':
                result[key] = value
            elif key in ('notes', 'gains', 'velocity', 'pins'):
                numbers = [int(part) for part in value.split(',')]
                result[key] = ([numbers[i:i + 3] for i in range(0, len(numbers), 3)]
                               if key == 'pins' else numbers)
            else:
                result[key] = int(value)
        return validate_config(result)
    except (ValueError, TypeError) as exc:
        raise ConfigError(str(exc)) from exc

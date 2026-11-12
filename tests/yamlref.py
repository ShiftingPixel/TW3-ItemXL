"""PyYAML with ItemXL's operation tags (!append, !scale, ...): an independent reader the tests compare the C++
YAML reader with."""
import copy
import math

import yaml

TAGS = ("append", "append-once", "prepend", "prepend-once", "remove", "scale", "offset", "set", "append-from",
        "prepend-from")


class Op:
    """A tagged value: !append, !scale, ..."""

    def __init__(self, op: str, value):
        self.op, self.value = op, value


class Loader(yaml.SafeLoader):
    pass


def _make(op):
    def construct(loader, node):
        if isinstance(node, yaml.SequenceNode):
            return Op(op, loader.construct_sequence(node, deep=True))
        if isinstance(node, yaml.MappingNode):
            return Op(op, loader.construct_mapping(node, deep=True))
        return Op(op, loader.construct_scalar(node) if node.value != "" else None)
    return construct


for _t in TAGS:
    Loader.add_constructor("!" + _t, _make(_t))


def load_all(stream) -> list:
    # an alias is the same object in PyYAML; every document is copied, as the C++ reader copies
    return [copy.deepcopy(d) for d in yaml.load_all(stream, Loader=Loader) if d]


def plain(v):
    """JSON-like: an operation as {"!op": value}, as itemxl-cli yaml prints it."""
    if isinstance(v, Op):
        return {"!" + v.op: plain(v.value)}
    if isinstance(v, dict):
        return {str(k): plain(x) for k, x in v.items()}
    if isinstance(v, list):
        return [plain(x) for x in v]
    if isinstance(v, float) and math.isnan(v):
        return "nan"
    return v

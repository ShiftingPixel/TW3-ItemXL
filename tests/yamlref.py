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
    def flatten_mapping(self, node):
        """Merge keys as the YAML merge spec (and PyYAML up to 6.0.3) has them: of `<<: [a, b]` the earlier mapping
        wins, the mapping's own keys win over all. PyYAML 6.0.4 drops a repeated key only when both key strings are
        the same Python object, so there a later mapping in the list can win by chance (seen with `weight`)."""
        merge = []
        index = 0
        while index < len(node.value):
            key_node, value_node = node.value[index]
            if key_node.tag == "tag:yaml.org,2002:merge":
                del node.value[index]
                if isinstance(value_node, yaml.MappingNode):
                    self.flatten_mapping(value_node)
                    merge.extend(value_node.value)
                elif isinstance(value_node, yaml.SequenceNode):
                    submerge = []
                    for subnode in value_node.value:
                        if not isinstance(subnode, yaml.MappingNode):
                            raise yaml.constructor.ConstructorError(
                                "while constructing a mapping", node.start_mark,
                                f"expected a mapping for merging, but found {subnode.id}", subnode.start_mark)
                        self.flatten_mapping(subnode)
                        submerge.append(subnode.value)
                    submerge.reverse()           # later pairs win when the mapping is built: the first list entry last
                    for value in submerge:
                        merge.extend(value)
                else:
                    raise yaml.constructor.ConstructorError(
                        "while constructing a mapping", node.start_mark,
                        f"expected a mapping or list of mappings for merging, but found {value_node.id}",
                        value_node.start_mark)
            elif key_node.tag == "tag:yaml.org,2002:value":
                key_node.tag = "tag:yaml.org,2002:str"
                index += 1
            else:
                index += 1
        if merge:
            node.value = merge + node.value


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

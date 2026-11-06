"""Server configuration builder (no YAML dependency)."""
import copy


def _scalar(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    return "'%s'" % str(v).replace("'", "''")


def dump(cfg):
    out = []
    for sect, body in cfg.items():
        lines = ["  %s: %s" % (k, _scalar(v)) for k, v in body.items()
                 if v is not None]
        if lines:
            out.append("%s:" % sect)
            out += lines
    return "\n".join(out) + "\n"


def default(bed):
    return {
        "main": {"workers": 1, "port": bed.port, "listen-address": "::"},
        "misc": {"log-file": "stderr", "log-level": "warn"},
    }


def build(bed, **over):
    """``build(bed, main={'workers': 4}, auth=None)``; a value of None
    removes the key, a section of None removes the section."""
    cfg = default(bed)
    for sect, body in over.items():
        if body is None:
            cfg.pop(sect, None)
            continue
        cfg.setdefault(sect, {}).update(copy.deepcopy(body))
    return cfg

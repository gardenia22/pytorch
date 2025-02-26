import re
from typing import Tuple, List, Iterable, Iterator, Callable, Sequence, TypeVar, Optional
from enum import Enum
import contextlib
import textwrap

# Safely load fast C Yaml loader/dumper if they are available
try:
    from yaml import CSafeLoader as Loader
except ImportError:
    from yaml import SafeLoader as Loader  # type: ignore[misc]

try:
    from yaml import CSafeDumper as Dumper
except ImportError:
    from yaml import SafeDumper as Dumper  # type: ignore[misc]
YamlDumper = Dumper

# A custom loader for YAML that errors on duplicate keys.
# This doesn't happen by default: see https://github.com/yaml/pyyaml/issues/165
class YamlLoader(Loader):
    def construct_mapping(self, node, deep=False):  # type: ignore[no-untyped-def]
        mapping = []
        for key_node, value_node in node.value:
            key = self.construct_object(key_node, deep=deep)  # type: ignore[no-untyped-call]
            assert key not in mapping, f"Found a duplicate key in the yaml. key={key}, line={node.start_mark.line}"
            mapping.append(key)
        mapping = super().construct_mapping(node, deep=deep)  # type: ignore[no-untyped-call]
        return mapping

# Many of these functions share logic for defining both the definition
# and declaration (for example, the function signature is the same), so
# we organize them into one function that takes a Target to say which
# code we want.
#
# This is an OPEN enum (we may add more cases to it in the future), so be sure
# to explicitly specify with Union[Literal[Target.XXX]] what targets are valid
# for your use.
Target = Enum('Target', (
    # top level namespace (not including at)
    'DEFINITION',
    'DECLARATION',
    # TORCH_LIBRARY(...) { ... }
    'REGISTRATION',
    # namespace { ... }
    'ANONYMOUS_DEFINITION',
    # namespace cpu { ... }
    'NAMESPACED_DEFINITION',
    'NAMESPACED_DECLARATION',
))

# Matches "foo" in "foo, bar" but not "foobar". Used to search for the
# occurrence of a parameter in the derivative formula
IDENT_REGEX = r'(^|\W){}($|\W)'

# TODO: Use a real parser here; this will get bamboozled
def split_name_params(schema: str) -> Tuple[str, List[str]]:
    m = re.match(r'(\w+)(\.\w+)?\((.*)\)', schema)
    if m is None:
        raise RuntimeError(f'Unsupported function schema: {schema}')
    name, _, params = m.groups()
    return name, params.split(', ')

T = TypeVar('T')
S = TypeVar('S')

# These two functions purposely return generators in analogy to map()
# so that you don't mix up when you need to list() them

# Map over function that may return None; omit Nones from output sequence
def mapMaybe(func: Callable[[T], Optional[S]], xs: Iterable[T]) -> Iterator[S]:
    for x in xs:
        r = func(x)
        if r is not None:
            yield r

# Map over function that returns sequences and cat them all together
def concatMap(func: Callable[[T], Sequence[S]], xs: Iterable[T]) -> Iterator[S]:
    for x in xs:
        for r in func(x):
            yield r

# Conveniently add error context to exceptions raised.  Lets us
# easily say that an error occurred while processing a specific
# context.
@contextlib.contextmanager
def context(msg_fn: Callable[[], str]) -> Iterator[None]:
    try:
        yield
    except Exception as e:
        # TODO: this does the wrong thing with KeyError
        msg = msg_fn()
        msg = textwrap.indent(msg, '  ')
        msg = f'{e.args[0]}\n{msg}' if e.args else msg
        e.args = (msg,) + e.args[1:]
        raise

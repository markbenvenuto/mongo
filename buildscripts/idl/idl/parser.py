import yaml

def parse(stream):
    """Parse a YAML document into an AST"""

    # This may throw
    nodes = yaml.compose(stream)

    return None


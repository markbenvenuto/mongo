import yaml

class ParserError(object):
    def __init(self, msg):
        pass

class ParserErrors(object):
    def __init(self):
        self._errors = []

    def add(self, node, msg):
        pass

def parse(stream):
    """Parse a YAML document into an AST"""

    # This may throw
    nodes = yaml.compose(stream)
    
    #print(dir(nodes))
    #print(nodes.__class__)
    #if not isinstance(nodes.value, yaml.nodes.MappingNode):
    if not nodes.id == "mapping":
        print("Fooooo")
        print(nodes)
        return

    return None


"""
At the top level of the repo checkout, users can maintain a config.txt file, to store arbitrary key-value pairs, in
the following format:

----------------------------------
# config.txt example
key1 = value1
key2=  value2  # some comment

 key3 =value3
----------------------------------

This allows for customization of things like output file locations without committing those customizations into the
repository.

The Config class defined here provides an API to access those key-value pairs.
"""
import os


DEFAULT_FILENAME = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'config.txt')


def decomment(line: str) -> str:
    """
    Strips pound-comments. TODO: do this better
    """
    pound = line.rfind('#')
    if pound != -1:
        return line[:pound]
    return line


class Config:
    _instance = None

    def __init__(self, filename: str = DEFAULT_FILENAME):
        self._dict = {}
        if not os.path.isfile(filename):
            return

        with open(filename, 'r') as f:
            for orig_line in f:
                line = decomment(orig_line)
                eq = line.find('=')
                assert eq != -1, orig_line
                key = line[:eq].strip()
                value = line[eq+1:].strip()
                assert key not in self._dict, key
                self._dict[key] = value

    def get(self, key: str, default_value=None):
        return self._dict.get(key, default_value)

    @staticmethod
    def instance() -> 'Config':
        if Config._instance is None:
            Config._instance = Config()
        return Config._instance
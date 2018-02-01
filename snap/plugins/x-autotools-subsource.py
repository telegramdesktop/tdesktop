import os
import snapcraft

from snapcraft.internal import sources
from snapcraft.plugins import autotools

class Dict2Object(object):
    def __init__(self, d):
        for k, v in d.items():
            setattr(self, k.replace('-', '_'), v)


class AutotoolsSubsourcePlugin(autotools.AutotoolsPlugin):

    @classmethod
    def schema(cls):
        schema = super().schema()

        schema['properties']['sub-sources'] = {
            'type': 'array',
            'minitems': 0,
            'uniqueItems': True,
            'items': {
                'type': 'object',
                'additionalProperties': True,
            },
            'default': [],
        }

        return schema

    @classmethod
    def get_pull_properties(cls):
        return [
            'sub-sources',
        ]

    def pull(self):
        super().pull()

        for src in self.options.sub_sources:
            [name] = src.keys()
            [values] = src.values()

            if 'source' in values:
                dest = values['dest'] if 'dest' in values else ''
                sources.get(os.path.join(self.sourcedir, dest),
                    os.path.join(self.build_basedir, dest),
                    Dict2Object(values))

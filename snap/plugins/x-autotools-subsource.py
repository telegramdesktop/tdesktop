# -*- Mode:Python; indent-tabs-mode:nil; tab-width:4 -*-
#
# Author: Marco Trevisan <marco@ubuntu.com>
# Copyright (C) 2017-2018 Canonical Ltd
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

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

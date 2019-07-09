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
import requests

from snapcraft.plugins import python

class PatchedPythonPlugin(python.PythonPlugin):

    @classmethod
    def schema(cls):
        schema = super().schema()

        schema['properties']['patches'] = {
            'type': 'array',
            'minitems': 0,
            'uniqueItems': True,
            'items': {
                'type': 'string',
            },
            'default': [],
        }

        schema['pull-properties'].extend([
            'patches',
        ])

        return schema

    def pull(self):
        super().pull()

        for patch in self.options.patches:
            patch_name = os.path.basename(patch)
            patch_stamp = os.path.join(
                self.sourcedir, '.snapcraft-patched-{}'.format(patch_name))

            if not os.path.exists(patch_stamp):
                if os.path.exists(patch):
                    patch_file = os.path.join(os.getcwd(), patch)
                else:
                    patch_file = os.path.join(
                        self.sourcedir, 'snapcraft-patch-{}'.format(patch_name))
                    with open(patch_file, 'wb') as file:
                        file.write(requests.get(patch).content)

                patch_cmd = 'git apply -v3 {}'.format(patch_file).split()
                self.run(patch_cmd, cwd=self.sourcedir)
                open(patch_stamp, 'a').close()

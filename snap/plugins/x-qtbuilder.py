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
import shutil
import snapcraft

from snapcraft.plugins import make

class QtBuilderPlugin(make.MakePlugin):

    @classmethod
    def schema(cls):
        schema = super().schema()

        schema['properties']['configflags'] = {
            'type': 'array',
            'minitems': 1,
            'uniqueItems': False,
            'items': {
                'type': 'string',
            },
            'default': [],
        }

        schema['properties']['qt-source-git'] = {
            'type': 'string'
        }

        schema['properties']['qt-source-depth'] = {
            'type': 'integer',
            'default': 1
        }

        schema['properties']['qt-version'] = {
            'type': 'string'
        }

        schema['properties']['qt-patches-base-url'] = {
            'type': 'string'
        }

        schema['properties']['qt-patches-path'] = {
            'type': 'string'
        }

        schema['properties']['qt-submodules'] = {
            'type': 'array',
            'minitems': 0,
            'uniqueItems': True,
            'items': {
                'type': 'string',
            },
            'default': [],
        }

        schema['properties']['qt-extra-plugins'] = {
            'type': 'array',
            'minitems': 0,
            'uniqueItems': True,
            'items': {
                'type': 'object',
                'minitems': 0,
                'uniqueItems': True,
                'items': {
                    'type': 'string',
                },
            },
            'default': [],
        }

        schema['properties']['environment'] = {
            'type': 'array',
            'minitems': 0,
            'uniqueItems': True,
            'items': {
                'type': 'object',
                'minitems': 0,
                'uniqueItems': True,
                'items': {
                    'type': 'string',
                },
            },
            'default': [],
        }

        schema['required'].append('qt-source-git')

        schema['build-properties'].append('configflags')

        return schema

    @classmethod
    def get_pull_properties(cls):
        return [
            'qt-version',
            'qt-patches-base-url',
            'qt-patches-path',
            'qt-submodules',
            'qt-extra-plugins',
        ]

    def __init__(self, name, options, project):
        super().__init__(name, options, project)
        self.build_packages.extend(['g++', 'patch', 'perl', 'wget'])
        self.options.source_depth = self.options.qt_source_depth

        if self.options.qt_version:
            if self.options.qt_version[0] == 'v':
                self.options.source_branch = self.options.qt_version
                self.options.qt_version = self.options.qt_version[1:]
            else:
                self.options.source_branch = '.'.join(
                    self.options.qt_version.split('.')[:-1])


    def pull(self):
        if not os.path.exists(os.path.join(self.sourcedir, '.git')) or \
           not os.path.exists(os.path.join(self.sourcedir, 'init-repository')):
            shutil.rmtree(self.sourcedir, ignore_errors=True)
            command = 'git clone {} {}'.format(
                self.options.qt_source_git, self.sourcedir).split()
            if self.options.source_branch:
                command.extend(['--branch', str(self.options.source_branch)])
            if self.options.source_depth:
                command.extend(['--depth', str(self.options.source_depth)])

            self.run(command)

        command = 'perl init-repository --branch -f'.split()
        if len(self.options.qt_submodules):
            command.extend('--module-subset={}'.format(
                ','.join(self.options.qt_submodules)).split())
        self.run(command, cwd=self.sourcedir)

        if self.options.qt_version:
            self.run("git submodule foreach git checkout v{}".format(
                self.options.qt_version).split(), self.sourcedir)

        patch_file_template = '${{name}}{}.diff'.format(
            '_' + self.options.qt_version.replace('.', '_') \
            if self.options.qt_version else '')

        if self.options.qt_patches_base_url:
            patch_uri_template = '{}/{}'.format(
                self.options.qt_patches_base_url, patch_file_template)

            patch_cmd = 'git submodule foreach -q'.split() + \
                        ['[ -e {touch_file} ] || ' \
                        'wget -q -O - {patch_uri_template} | patch -p1 && ' \
                        'touch {touch_file}'.format(
                            patch_uri_template=patch_uri_template,
                            touch_file='.snapcraft-qt-patched')]

            self.run(patch_cmd, cwd=self.sourcedir)

        if self.options.qt_patches_path:
            patch_path_template = os.path.join(
                os.getcwd(), self.options.qt_patches_path, patch_file_template)

            patch_cmd = 'git submodule foreach -q'.split() + \
                        ['[ -e {patch} ] && git apply -v3 {patch} || true'.format(
                            patch=patch_path_template)]

            self.run(patch_cmd, cwd=self.sourcedir)

        for extra_plugin in self.options.qt_extra_plugins:
            [framework] = list(extra_plugin)

            final_path = os.path.join(self.sourcedir, 'qtbase', 'src',
                'plugins', framework)

            for repo in extra_plugin[framework]:
                repo_path = os.path.basename(repo)
                if repo_path.endswith('.git'):
                    repo_path = repo_path[:-4]

                if not os.path.exists(os.path.join(final_path, repo_path)):
                    command = 'git clone {}'.format(repo).split()
                    self.run(command, cwd=final_path)

    def build(self):
        env = {}

        for environ in self.options.environment:
            [env_name] = list(environ)
            env[env_name] = str(environ[env_name])

        self.run(['./configure'] + self.options.configflags, env=env)
        super().build()

import os
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

        schema['required'].append('qt-source-git')
        schema['required'].append('qt-version')

        schema['build-properties'].append('configflags')

        return schema

    @classmethod
    def get_pull_properties(cls):
        return [
            'qt-version',
            'qt-patches-base-url',
            'qt-patches-path',
            'qt-submodules',
        ]

    def __init__(self, name, options, project):
        super().__init__(name, options, project)
        self.build_packages.extend(['patch', 'perl', 'wget'])
        self.options.source_branch = self.options.qt_version
        self.options.source_depth = self.options.qt_source_depth

    def pull(self):
        if not os.path.exists(os.path.join(self.sourcedir, '.git')) or \
           not os.path.exists(os.path.join(self.sourcedir, 'init-repository')):
            self.run('rm -rf {}'.format(self.sourcedir).split())
            command = 'git clone {} {}'.format(
                self.options.qt_source_git, self.sourcedir).split()
            if self.options.source_branch:
                command.extend(['--branch', self.options.source_branch])
            if self.options.source_depth:
                command.extend(['--depth', str(self.options.source_depth)])

            self.run(command)

        command = 'perl init-repository -f'.split()
        if len(self.options.qt_submodules):
            command.extend('--module-subset={}'.format(
                ','.join(self.options.qt_submodules)).split())
        self.run(command, cwd=self.sourcedir)

        if self.options.qt_version:
            self.run("git submodule foreach git checkout v{}".format(
                self.options.qt_version).split(), self.sourcedir)

        if self.options.qt_patches_base_url:
            patch_uri_template = '{}/${{name}}_{}.diff'.format(
                self.options.qt_patches_base_url,
                self.options.qt_version.replace('.', '_'))

            patch_cmd = 'git submodule foreach -q'.split() + \
                        ['[ -e {touch_file} ] || ' \
                        'wget -q -O - {patch_uri_template} | patch -p1 && ' \
                        'touch {touch_file}'.format(
                            patch_uri_template=patch_uri_template,
                            touch_file='.snapcraft-qt-patched')]

            self.run(patch_cmd, cwd=self.sourcedir)

        if self.options.qt_patches_path:
            patch_uri_template = os.path.join(
                os.getcwd(), self.options.qt_patches_path,
                '${{name}}_{}.diff'.format(self.options.qt_version.replace('.', '_')))

            patch_cmd = 'git submodule foreach -q'.split() + \
                        ['[ -e {patch} ] && git apply {patch} || true'.format(
                            patch=patch_uri_template)]

            self.run(patch_cmd, cwd=self.sourcedir)

    def build(self):
        self.run(['./configure'] + self.options.configflags)
        super().build()

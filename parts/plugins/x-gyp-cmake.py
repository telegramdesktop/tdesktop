import os
import snapcraft

from snapcraft.plugins import cmake


class GypCMakePlugin(cmake.CMakePlugin):
    """A basic plugin for snapcraft that generates CMake files from gyp"""

    @classmethod
    def schema(cls):
        schema = super().schema()

        schema['properties']['gyp-file'] = {
            'type': 'string'
        }

        schema['properties']['build-type'] = {
            'type': 'string',
            'default': 'Release',
            'enum': ['Debug', 'Release'],
        }

        schema['required'].append('gyp-file')

        schema['build-properties'].extend([
            'build-type',
            'gyp-file',
        ])

        return schema

    def __init__(self, name, options, project):
        super().__init__(name, options, project)
        self.build_packages.extend([
            'binutils',
            'python',
        ])
        self.builddir = os.path.join(
            self.build_basedir, 'out', self.options.build_type)

    def build(self):
        env = self._build_environment()
        gyp_path = os.path.join(self.sourcedir, os.path.dirname(self.options.gyp_file))

        if not os.path.exists(os.path.join(self.builddir, 'CMakeLists.txt')):
            gyp_command = ['gyp'] + self.options.configflags + ['--format=cmake']
            gyp_command.append('--generator-output={}'.format(self.build_basedir))
            gyp_command.append(os.path.basename(self.options.gyp_file))
            self.run(gyp_command, cwd=gyp_path)

        if not os.path.exists(os.path.join(self.builddir, 'Makefile')):
            self.run(['cmake', '.'], env=env)

        self.make(env=env)

        if self.options.artifacts and self.options.build_type == 'Release':
            for artifact in self.options.artifacts:
                dest = os.path.join(self.installdir, artifact)
                if os.path.isfile(dest):
                    mime_type = self.run_output(
                        'file --mime-type -b {}'.format(dest).split())
                    if 'application/x-executable' in mime_type:
                        self.run(['strip', dest])

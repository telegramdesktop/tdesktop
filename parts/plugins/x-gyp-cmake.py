import os
import snapcraft


class GypCMakePlugin(snapcraft.BasePlugin):
    """A basic plugin for snapcraft that generates CMake files from gyp"""

    @classmethod
    def schema(cls):
        schema = super().schema()
        schema['properties']['configflags'] = {
            'type': 'array',
            'minitems': 1,
            'uniqueItems': True,
            'items': {
                'type': 'string',
            },
            'default': [],
        }

        schema['properties']['gyp-file'] = {
            'type': 'string'
        }

        schema['properties']['build-type'] = {
            'type': 'string',
            'default': 'Release',
            'enum': ['Debug', 'Release'],
        }

        schema['properties']['built-binaries'] = {
            'type': 'array',
            'minitems': 0,
            'uniqueItems': True,
            'items': {
                'type': 'string',
            },
            'default': [],
        }

        schema['required'].append('gyp-file')
        schema['build-properties'].extend(['configflags', 'gyp-file', 'build-type'])

        return schema

    def __init__(self, name, options, project):
        super().__init__(name, options, project)
        self.build_packages.extend([
            'binutils',
            'cmake',
            'python',
        ])

    def build(self):
        super().build()

        gyp_path = os.path.join(self.sourcedir, os.path.dirname(self.options.gyp_file))
        build_dir = os.path.join(self.build_basedir, 'out', self.options.build_type)

        if not os.path.exists(os.path.join(build_dir, 'CMakeLists.txt')):
            gyp_command = ['gyp'] + self.options.configflags + ['--format=cmake']
            gyp_command.append('--generator-output={}'.format(self.build_basedir))
            gyp_command.append(os.path.basename(self.options.gyp_file))

            self.run(gyp_command, cwd=gyp_path)

        if not os.path.exists(os.path.join(build_dir, 'Makefile')):
            self.run(['cmake', '.'], cwd=build_dir)

        self.run(['make', '-j{}'.format(self.parallel_build_count)], cwd=build_dir)

        if len(self.options.built_binaries):
            for bin in self.options.built_binaries:
                dest = os.path.join(self.installdir, 'bin', os.path.basename(bin))
                snapcraft.file_utils.link_or_copy(
                    os.path.join(build_dir, bin), dest, follow_symlinks=True)

                if self.options.build_type == 'Release':
                    mime_type = self.run_output('file --mime-type -b {}'.format(dest).split())
                    if 'application/x-executable' in mime_type:
                        self.run(['strip', dest])
        else:
            self.run(['make', 'install', 'DESTDIR={}'.format(
                self.installdir)], cwd=build_dir)

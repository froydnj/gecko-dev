# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import unicode_literals

import os

from mozbuild.makeutil import read_dep_makefile
import mozpack.path as mozpath
from mozpack.copier import FileRegistry
from mozpack.manifests import InstallManifest

def find_deps_files(path):
    """Find paths to Make dependency files.

    This is an iterator of (objdir, deps_path).
    """
    for root, dirs, files in os.walk(path):
        root = mozpath.normpath(root)
        if mozpath.basename(root) != '.deps':
            continue

        parent = os.path.dirname(root)

        for f in files:
            if f.endswith('.pp'):
                yield parent, mozpath.join(root, f)

class Dependencies(object):
    """Data structure to hold basic build system dependencies.

    This isn't a truly generic class. It is optimized to hold results from
    ``load_deps_files()``.
    """

    def __init__(self, topsrcdir, topobjdir):
        self.topsrcdir = topsrcdir
        self.topobjdir = topobjdir
        self.targets = {}
        self.dependencies = {}
        self._realpath_cache = {}
        self._filemap = None

    def load_deps_file(self, objdir, fh):
        """Load a single dependency file."""
        for rule in read_dep_makefile(fh):
            for target in rule.targets():
                full_target = mozpath.normpath(mozpath.join(objdir,
                    target))

                normalized_deps = []
                for d in rule.dependencies():
                    full_depend = mozpath.join(objdir, d)
                    # Resolve symbolic links from $objdir/dist/include and
                    # the like to their srcdir equivalents.  Don't use
                    # _realpath_cache.get(full_depend, os.path.realpath(...)),
                    # as the whole point of this cache is to avoid hitting
                    # the filesystem if we don't have to.
                    if full_depend in self._realpath_cache:
                        full_depend = self._realpath_cache[full_depend]
                    else:
                        resolved = os.path.realpath(full_depend)
                        self._realpath_cache[full_depend] = resolved
                        full_depend = resolved
                    normalized_deps.append(full_depend)
                    self.dependencies.setdefault(full_depend, set()).add(full_target)

                assert full_target not in self.targets
                self.targets[full_target] = normalized_deps

    def prune_system_paths(self):
        """Obtain a Dependencies with system paths pruned."""
        allowed = (self.topsrcdir, self.topobjdir)

        newtargets = {}
        newdepends = {}

        for target, depends in self.targets.iteritems():
            if not target.startswith(allowed):
                continue

            depends = [d for d in depends if d.startswith(allowed)]
            newtargets[target] = depends

        for depend, targets in self.dependencies.iteritems():
            if not depend.startswith(allowed):
                continue

            targets = {t for t in targets if t.startswith(allowed)}
            newdepends[depend] = targets

        deps = Dependencies(self.topsrcdir, self.topobjdir)
        deps.targets = newtargets
        deps.dependencies = newdepends

        return deps

    def resolve_srcdir_paths(self):
        """Obtain a Dependencies with objdir paths resolved to srcdir equivalents.

        Dependencies often reference paths in the objdir. For example, headers
        are often included from ``objdir/dist/include``. Calling this function
        will rewrite paths to the srcdir equivalent, where possible.
        """
        files = self.filemap
        newtargets = {}
        newdepends = {}

        for target, depends in self.targets.iteritems():
            target = files.get(target, target)
            depends = [files.get(d, d) for d in depends]
            newtargets[target] = depends

        for depend, targets in self.dependencies.iteritems():
            depend = files.get(depend, depend)
            targets = {files.get(t, t) for t in targets}
            newdepends[depend] = targets

        deps = Dependencies(self.topsrcdir, self.topobjdir)
        deps.targets = newtargets
        deps.dependencies = newdepends

        return deps

    @property
    def filemap(self):
        if self._filemap is None:
            self._filemap = get_srcdir_objdir_file_map(self.topobjdir)

        return self._filemap

    def get_source_file(self, target):
        """Try to obtain the corresponding source file for a target."""
        deps = self.targets.get(target)
        if not deps:
            return None

        source = deps[0]
        # The suffix list should probably come from elsewhere.
        if not source.endswith(('.c', '.cpp', '.cc', '.cxx', '.m', '.mm', '.s', '.S')):
            return None

        return source

    def get_targets(self, path, resolve_source=False):
        targets = self.dependencies.get(path, set())
        resolved = self.resolve_srcdir_paths()
        targets |= resolved.dependencies.get(path, set())

        # Find references to objdir equivalent to this file or vice-versa.
        if path.startswith(self.topobjdir):
            # We would have addressed this above.
            pass
        elif path.startswith(self.topsrcdir):
            reversemap = {v: k for k, v in self.filemap.iteritems()}
            obj_path = reversemap.get(path)
            if obj_path:
                targets |= self.dependencies.get(obj_path, set())

        if resolve_source:
            newtargets = set()
            for t in targets:
                s = self.get_source_file(t)
                if s:
                    t = s

                newtargets.add(t)

            targets = newtargets

        return targets

def get_install_manifests(topobjdir):
    """Obtain InstallManifest and prefix metadata."""
    man_dir = mozpath.join(topobjdir, '_build_manifests', 'install')
    j = mozpath.join
    for f in sorted(os.listdir(man_dir)):
        full = mozpath.join(man_dir, f)

        if f == 'tests':
            yield (j(topobjdir, '_tests'), InstallManifest(path=full))
        elif f == 'xpidl':
            yield (j(topobjdir, 'config/makefiles/xpidl'), InstallManifest(path=full))
        elif f.startswith('dist_'):
            yield (j(topobjdir, f.replace('_', '/')), InstallManifest(path=full))
        else:
            raise Exception('Unknown install manifest encountered: %s' % f)

def get_srcdir_objdir_file_map(topobjdir):
    """Obtains a mapping of filenames in the objdir to their srcdir equivalent."""
    filemap = {}
    for root, m in get_install_manifests(topobjdir):
        r = FileRegistry()
        m.populate_registry(r)
        for p, f in r:
            full_objdir = mozpath.normpath(mozpath.join(root, p))
            if not hasattr(f, 'path'):
                continue

            filemap[full_objdir] = f.path

    return filemap

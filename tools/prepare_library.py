"""Stage sources beside the selected archives without changing the checkout."""
import argparse
import shutil
from build_config import (ROOT, package_files, add_core_argument, library_dir,
                          version_header, verify_archive)


def prepare(core):
    for target in ('esp32', 'esp32s3'):
        verify_archive(core, target)
    dest = library_dir(core)
    for f in package_files(ROOT):
        relative = f.relative_to(ROOT)
        if relative.parts[:2] in [('src', 'esp32'), ('src', 'esp32s3')]:
            # Archives are untracked build output, so anything here is a local leftover that may
            # belong to a different core; only the ones just built for this one may be staged.
            continue
        target = dest / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, target)
    (dest / 'src/SinricProWebRTCVersion.h').write_text(version_header(core))
    properties = dest / 'library.properties'
    properties.write_text(properties.read_text().replace('Requires ESP32 core 3.3.11.',
                                                        f'Requires ESP32 core {core}.'))
    readme = dest / 'README.md'
    readme.write_text(f'> This ZIP was built for Arduino ESP32 **{core}**. '
                      'Select this exact core version in Boards Manager. '
                      'See the per-target build-info.json files for dependency revisions.\n\n' + readme.read_text())
    print(dest)
    return dest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    add_core_argument(parser)
    prepare(parser.parse_args().core_version)

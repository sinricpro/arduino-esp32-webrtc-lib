"""Copy the freshly built default-core archives over the ones the repository ships."""
import shutil
from build_config import DEFAULT_CORE, ROOT, TARGETS, library_dir, verify_archive

for target in TARGETS:
    verify_archive(DEFAULT_CORE, target)  # Never ship an archive that disagrees with its manifest.
    dest = ROOT / 'src' / target
    dest.mkdir(parents=True, exist_ok=True)
    for name in ('libsinric_webrtc.a', 'build-info.json'):
        shutil.copy2(library_dir(DEFAULT_CORE) / 'src' / target / name, dest / name)
    print(dest)

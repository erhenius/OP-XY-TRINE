"""Build the M3 Trine filter with a local OP-XY toolchain."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

REV = 'a07b08faeb58cc7e74cdbc8bd98f809a8ffd5159'
HERE = Path(__file__).resolve().parent


def run(*args, cwd=None):
    subprocess.run([str(arg) for arg in args], cwd=cwd, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=Path, help='prepared SDK directory')
    parser.add_argument('--tools', type=Path, help='compiler bundle with bin/ (default SDK/local/toolchain)')
    parser.add_argument('--out', required=True, type=Path, help='fresh build directory')
    parser.add_argument('--stock-os', type=Path, help='local stock OS 1.2.1; omit for module-only compile')
    args = parser.parse_args()
    prepared = args.sdk.resolve()
    tools = (args.tools or prepared/'local/toolchain').resolve()
    out = args.out.resolve()
    if out.exists():
        parser.error('--out must be a new directory')
    if not (tools/'bin').is_dir():
        parser.error('Specify your prepared Blackfin bundle with --tools')
    if args.stock_os and not args.stock_os.is_file():
        parser.error('--stock-os must name your local stock os.ldr')
    with tempfile.TemporaryDirectory(prefix='opxy-m3-') as temporary:
        sdk = Path(temporary)/'sdk'
        local_ref = subprocess.run(['git','-C',str(prepared),'cat-file','-e',REV+'^{commit}'],
                                   capture_output=True).returncode == 0
        origin = prepared if local_ref else 'https://github.com/etcetc000/open-xy.git'
        run('git','clone','--no-checkout',origin,sdk)
        run('git','checkout','--detach',REV,cwd=sdk)
        run('git','apply','--check',HERE/'instrument-m3.patch',cwd=sdk)
        run('git','apply',HERE/'instrument-m3.patch',cwd=sdk)
        if args.stock_os:
            sys.path.insert(0, str(sdk))
            from copy_sites import prepare
            prepare(sdk, args.stock_os)
        verify_stock = ['--stock-os', args.stock_os.resolve()] if args.stock_os else []
        run(sys.executable,HERE/'verify.py',sdk,*verify_stock)
        native = ['build','--stock-os',args.stock_os.resolve()] if args.stock_os else ['compile']
        run(sys.executable,sdk/'sdk.py',*native,'--machine',HERE/'m3probe',
            '--layout',HERE/'probe-layout.json','--compiler',tools,'--toolchain',tools/'bin',
            '--profiling','off','--out',out,cwd=sdk)


if __name__ == '__main__':
    main()

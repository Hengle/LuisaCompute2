import multiprocessing
import os
import sys
from subprocess import Popen, call, DEVNULL
from typing import List

ALL_FEATURES = ['dsl', 'python', 'gui', 'cuda', 'cpu', 'remote', 'dx', 'metal', 'vulkan']


def get_default_features():
    # CPU and Remote are always enabled
    features = ['dsl', 'python', 'gui']
    if call(['rustc', '--version'], stdout=DEVNULL, stderr=DEVNULL) == 0:
        features.append('cpu')
        features.append('remote')
    # enable DirectX on Windows by default
    if sys.platform == 'win32':
        features.append('dx')
    # enable Metal on macOS by default
    if sys.platform == 'darwin':
        features.append('metal')
    # enable CUDA if available
    try:
        if 'CUDA_PATH' in os.environ or call(['nvcc', '--version'], stdout=DEVNULL, stderr=DEVNULL) == 0:
            features.append('cuda')
    except FileNotFoundError:
        pass
    try:
        if 'VULKAN_SDK' in os.environ or 'VK_SDK_PATH' in os.environ or call(['vulkaninfo'], stdout=DEVNULL, stderr=DEVNULL) == 0:
            features.append('vulkan')
    except FileNotFoundError:
        pass
    return features


def get_all_dependencies() -> List[str]:
    return ['rust']


def default_config():
    return {
        'cmake_args': [],
        'xmake_args': [],
        'build_system': 'cmake',
        'features': get_default_features(),
        'output': 'build',
    }


platform = sys.platform


def install_dep(dep: str):
    if dep == 'rust':
        if platform == 'win32':
            # download https://static.rust-lang.org/rustup/dist/i686-pc-windows-gnu/rustup-init.exe
            os.system(
                'curl -sSf https://static.rust-lang.org/rustup/dist/i686-pc-windows-gnu/rustup-init.exe -o rustup-init.exe')
            os.system('rustup-init.exe -y')
        elif platform == 'linux' or platform == 'darwin':
            os.system('curl https://sh.rustup.rs -sSf | sh -s -- -y')
        else:
            raise ValueError(f'Unknown platform: {platform}')
    else:
        raise ValueError(f'Unknown dependency: {dep}')


def get_config():
    config = default_config()
    # check if config.json exists
    if os.path.exists('config.json'):
        import json
        with open('config.json', 'r') as f:
            config.update(json.load(f))
    return config


def print_help():
    print('Usage: python bootstrap.py [build system] [mode] [options]')
    print('Build system:')
    print('  cmake                  Use CMake')
    print('  xmake                  Use xmake')
    print('Mode:')
    print('  release                Release mode (default)')
    print('  debug                  Debug mode')
    print('  reldbg                 Release with debug infomation mode')
    print('Options:')
    print('  --config   | -c        Configure build system')
    print('  --features | -f [[no-]features]  Add/remove features')
    print('      Features:')
    print('          all                Enable all features listed below that are detected available')
    print('          [no-]dsl           Enable (disable) C++ DSL support')
    print('          [no-]python        Enable (disable) Python support')
    print('          [no-]gui           Enable (disable) GUI support')
    print('          [no-]cuda          Enable (disable) CUDA backend')
    print('          [no-]cpu           Enable (disable) CPU backend')
    print('          [no-]remote        Enable (disable) remote backend')
    print('          [no-]dx            Enable (disable) DirectX backend')
    print('          [no-]metal         Enable (disable) Metal backend')
    print('          [no-]vulkan        Enable (disable) Vulkan backend')
    print('  --mode    | -m [node]  Build mode')
    print('      Modes:')
    print('          debug              Debug mode')
    print('          release            Release mode')
    print('          relwithdebuginfo   Release with debug infomation mode')
    print('  --build   | -b [N]     Build (N = number of jobs)')
    print('  --clean   | -C         Clean build directory')
    print('  --install | -i [deps]  Install dependencies')
    print('      Dependencies:')
    print('          all                Install all dependencies as listed below')
    print('          rust               Install Rust toolchain')
    print('  --output  | -o         Path to output directory')
    print('  -- [args]              Pass arguments to build system')


def dump_cmake_options(config: dict):
    with open("options.cmake.template") as f:
        options = f.read()
    for feature in config['features']:
        options = options.replace(f'[[feature_{feature}]]', 'ON')
    for feature in ALL_FEATURES:
        if feature not in config['features']:
            options = options.replace(f'[[feature_{feature}]]', 'OFF')
    with open("options.cmake", 'w') as f:
        f.write(options)


def dump_xmake_options(config: dict):
    # TODO: @Maxwell help pls
    pass


def dump_build_system_options(config: dict):
    build_sys = config['build_system']
    if build_sys == 'cmake':
        dump_cmake_options(config)
    elif build_sys == 'xmake':
        dump_xmake_options(config)
    else:
        raise ValueError(f'Unknown build system: {build_sys}')


def build_system_args_cmake(config: dict, mode: str) -> List[str]:
    args = config['cmake_args']
    cmake_mode = {
        'debug': 'Debug',
        'release': 'Release',
        'reldbg': 'RelWithDebInfo'
    }
    args.append(f'-DCMAKE_BUILD_TYPE={cmake_mode[mode]}')
    return args


def build_system_args_xmake(config: dict, mode: str) -> List[str]:
    args = config['xmake_args']
    xmake_mode = {
        'debug': 'debug',
        'release': 'release',
        'reldbg': 'releasedbg'
    }
    args.append(f'-m {xmake_mode[mode]}')
    # TODO: @Maxwell help pls
    return args


def build_system_args(config, mode) -> List[str]:
    if config['build_system'] == 'cmake':
        return build_system_args_cmake(config, mode)
    elif config['build_system'] == 'xmake':
        return build_system_args_xmake(config, mode)
    else:
        raise ValueError(f'Unknown build system: {config["build_system"]}')


submods = [
    'corrosion',
    'EASTL',
    ## TODO: add more submodules here
]


def init_submodule():
    if os.path.exists('.git'):
        os.system('git submodule update --init --recursive')
    else:
        for s in submods:
            if not os.path.exists(f'src/ext/{s}'):
                print(f'Fatal error: submodule in src/ext/{s} not found.', file=sys.stderr)
                print('Please clone the repository with --recursive option.', file=sys.stderr)
                sys.exit(1)


def main(args: List[str]):
    init_submodule()
    if len(args) == 1:
        print_help()
        return
    config = get_config()
    for i, arg in enumerate(args):
        if arg.lower() in ('cmake', 'xmake'):
            config['build_system'] = arg.lower()
            args.pop(i)
            break
    mode = "release"
    for i, arg in enumerate(args):
        if arg.lower() in ('debug', 'release', 'reldbg'):
            mode = arg.lower()
            args.pop(i)
            break
    i = 1
    run_config = False
    run_build = False
    build_jobs = multiprocessing.cpu_count()
    while i < len(args):
        opt = args[i]
        if opt == '--clean' or opt == '-C':
            if os.path.exists(config['output']):
                import shutil
                shutil.rmtree(config['output'])
            if os.path.exists('options.cmake.cli'):
                os.remove('options.cmake.cli')
            if os.path.exists('options.xmake.cli'):
                os.remove('options.xmake.cli')
            return
        elif opt == '--help' or opt == '-h':
            print_help()
            return
        elif opt == '--config' or opt == '-c':
            run_config = True
            i += 1
        elif opt == '--build' or opt == '-b':
            run_build = run_config = True
            i += 1
            if i < len(args) and not args[i].startswith('-'):
                build_jobs = int(args[i])
                i += 1
        elif opt == '--features' or opt == '-f':
            i += 1
            while i < len(args) and not args[i].startswith('-'):
                f = args[i].lower()
                if f == 'all':
                    config['features'] = get_default_features()
                elif f.startswith('no-'):
                    f = f[3:]
                    if f in config['features']:
                        config['features'].remove(f)
                else:
                    if f not in config['features']:
                        config['features'].append(f)
                i += 1
        elif opt == '--install' or opt == '-i':
            i += 1
            deps = []
            while i < len(args) and not args[i].startswith('-'):
                deps.append(args[i].lower())
                i += 1
            if "all" in deps:
                deps = get_all_dependencies()
            for d in deps:
                install_dep(d)
        elif opt == '--output' or opt == '-o':
            config['output'] = args[i + 1]
            i += 2
        elif opt == "--":
            if config['build_system'] == 'cmake':
                config['cmake_args'] = args[i + 1:]
            elif config['build_system'] == 'xmake':
                config['xmake_args'] = args[i + 1:]
            else:
                raise ValueError(f'Unknown build system: {config["build_system"]}')
            break
        else:
            raise ValueError(f'Unknown option: {opt}')
    # write config.json
    import json
    with open('config.json', 'w') as f:
        json.dump(config, f, indent=4)

    # dump build system options, e.g., options.cmake and options.lua
    dump_build_system_options(config)

    # config and build
    output = config['output']
    if run_config or run_build:
        if not os.path.exists(output):
            os.mkdir(output)

    # config build system
    if run_config:
        args = build_system_args(config, mode)
        if config['build_system'] == 'cmake':
            p = Popen(['cmake', '..'] + args, cwd=output)
            p.wait()
        elif config['build_system'] == 'xmake':
            p = Popen(['xmake', 'f'] + args)
            p.wait()
        else:
            raise ValueError(f'Unknown build system: {config["build_system"]}')
    if run_build:
        if config['build_system'] == 'cmake':
            p = Popen(['cmake', '--build', '.', '-j', str(build_jobs)], cwd=output)
            p.wait()
        elif config['build_system'] == 'xmake':
            os.system('xmake build')
        else:
            raise ValueError(f'Unknown build system: {config["build_system"]}')


if __name__ == '__main__':
    main(sys.argv)

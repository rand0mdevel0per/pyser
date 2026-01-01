import os
import sys
import platform
import subprocess
import shlex
from pathlib import Path
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext
import shutil

here = Path(__file__).resolve().parent
package_dir = here / "pyserpy"

class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        super().__init__(name, sources=[])
        self.sourcedir = os.fspath(Path(sourcedir).resolve())

class CMakeBuild(build_ext):
    def run(self):
        try:
            subprocess.check_output(["cmake", "--version"])
        except Exception:
            raise RuntimeError("CMake must be installed to build the following extensions: " + ", ".join(e.name for e in self.extensions))
        for ext in self.extensions:
            self.build_extension(ext)

    def _find_vcpkg_toolchain(self):
        vcpkg_exe = shutil.which("vcpkg")
        if vcpkg_exe:
            root = Path(vcpkg_exe).resolve().parent
            cand = root / "scripts" / "buildsystems" / "vcpkg.cmake"
            if cand.exists():
                return str(cand)
            cand2 = root.parent / "scripts" / "buildsystems" / "vcpkg.cmake"
            if cand2.exists():
                return str(cand2)
        if os.name == "nt":
            try:
                out = subprocess.check_output(["where", "vcpkg"], stderr=subprocess.DEVNULL, shell=False, text=True)
                for line in out.splitlines():
                    p = Path(line.strip())
                    if p.exists():
                        cand = p.parent / "scripts" / "buildsystems" / "vcpkg.cmake"
                        if cand.exists():
                            return str(cand)
                        cand2 = p.parent.parent / "scripts" / "buildsystems" / "vcpkg.cmake"
                        if cand2.exists():
                            return str(cand2)
            except Exception:
                pass
        vroot = os.environ.get("VCPKG_ROOT") or os.environ.get("VCPKG_HOME")
        if vroot:
            cand = Path(vroot) / "scripts" / "buildsystems" / "vcpkg.cmake"
            if cand.exists():
                return str(cand)
        return None

    def build_extension(self, ext):
        extdir = os.fspath(Path(self.get_ext_fullpath(ext.name)).parent.resolve())

        cfg = "Debug" if self.debug else "Release"

        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DCMAKE_BUILD_TYPE={cfg}",
        ]

        cmake_args += [f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{cfg.upper()}={extdir}"]

        vcpkg_toolchain = self._find_vcpkg_toolchain()
        if vcpkg_toolchain:
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={shlex.quote(vcpkg_toolchain)}")
            print(f"Using vcpkg toolchain: {vcpkg_toolchain}")
        else:
            print("vcpkg toolchain not found; proceeding without it (ensure vcpkg libs are available on your system)")

        build_temp = Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        # Use explicit -S (source) and -B (build) to configure into the build
        # directory. This avoids relying on cwd behavior and is robust on
        # Windows PowerShell.
        subprocess.check_call(["cmake", "-S", ext.sourcedir, "-B", str(build_temp)] + cmake_args)

        # Now build the configured tree explicitly (no cwd). This calls the
        # same underlying generator's build step and avoids double-path issues.
        build_cmd = ["cmake", "--build", str(build_temp)]
        if not self.compiler or platform.system() == "Windows":
            build_cmd += ["--config", cfg]
        subprocess.check_call(build_cmd)

        found = []
        for pattern in ("*.pyd", "*.so", "*.dll"):
            for p in Path(build_temp).rglob(pattern):
                if "pyser" in p.name.lower():
                    found.append(p)
        for pattern in ("*.pyd", "*.so", "*.dll"):
            for p in Path(extdir).glob(pattern):
                if "pyser" in p.name.lower():
                    found.append(p)

        if not found:
            print("Warning: built library not found automatically; you may need to copy the produced shared library into the package directory manually.")
        else:
            dest = Path(package_dir)
            dest.mkdir(parents=True, exist_ok=True)
            for src in found:
                try:
                    dst_path = dest / src.name
                    print(f"Copying {src} -> {dst_path}")
                    shutil.copy2(str(src), str(dst_path))
                except:
                    print(f"Failed to copy {src}")


long_description = "Python wrapper for the pyser C++ extension"
if (here / "README.md").exists():
    long_description = (here / "README.md").read_text(encoding="utf-8")

setup(
    name="rkyv",
    version="1.0.1",
    description="A high-performance C++ pyser serialization library with python wrapping",
    long_description=long_description,
    long_description_content_type="text/markdown",
    packages=find_packages(exclude=("tests",)),
    # CMakeLists.txt lives in the 'cpp' subdirectory — point the extension there
    ext_modules=[CMakeExtension("pyser", sourcedir=str(here / "cpp"))],
    cmdclass={"build_ext": CMakeBuild},
    include_package_data=True,
    zip_safe=False,
)

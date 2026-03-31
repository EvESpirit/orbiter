import os
import sys
import subprocess
import shutil
import urllib.request
import urllib.error
import stat
import time
import json
import ssl
import threading
import concurrent.futures

try:
    _create_unverified_https_context = ssl._create_unverified_context
except AttributeError:
    pass
else:
    ssl._create_default_https_context = _create_unverified_https_context

# ANSI escape
if sys.platform == 'win32':
    os.system("")

class Colors:
    HEADER = '\033[95m'    # Magenta
    INFO = '\033[96m'      # Cyan
    SUCCESS = '\033[92m'   # Green
    WARNING = '\033[93m'   # Yellow
    ERROR = '\033[91m'     # Red
    RESET = '\033[0m'      # Reset

print_lock = threading.Lock()

def ts_print(*args, **kwargs):
    with print_lock:
        print(*args, **kwargs)

def printHeader(text): ts_print(f"\n{Colors.HEADER}{text}{Colors.RESET}")
def printSuccess(text): ts_print(f"{Colors.SUCCESS}[SUCCESS]  {text}{Colors.RESET}")
def printWarning(text): ts_print(f"{Colors.WARNING}[WARNING]  {text}{Colors.RESET}")
def printError(text): ts_print(f"{Colors.ERROR}[ERROR]  {text}{Colors.RESET}")
def printInfo(text): ts_print(f"{Colors.INFO}{text}{Colors.RESET}")

def removeReadOnly(action, filePath, _):
    os.chmod(filePath, stat.S_IWRITE)
    action(filePath)

def runCommand(commandArgs, workingDirectory=None, ignoreExitCode=False, quiet=False):
    commandString = ' '.join(str(arg) for arg in commandArgs)
    printInfo(f"[Running]: {commandString}")
    
    try:
        if quiet:
            executionResult = subprocess.run(commandArgs, cwd=workingDirectory, capture_output=True, text=True)
        else:
            executionResult = subprocess.run(commandArgs, cwd=workingDirectory)
        
        if executionResult.returncode != 0 and not ignoreExitCode:
            printError(f"Command failed with exit code {executionResult.returncode}.")
            
            if quiet:
                ts_print(f"\n{Colors.ERROR}OUTPUT LOG{Colors.RESET}")
                if executionResult.stdout: ts_print(executionResult.stdout)
                if executionResult.stderr: ts_print(executionResult.stderr)
                ts_print(f"{Colors.ERROR}------------------------------{Colors.RESET}")
                
            sys.exit(executionResult.returncode)
            
        return executionResult.returncode
        
    except FileNotFoundError as error:
        printError(f"Command executable not found: {error}")
        sys.exit(1)

def downloadWithRetry(downloadUrl, destinationFile, toolName, expectedSizeMb=None, maxRetries=3):
    for attempt in range(1, maxRetries + 1):
        try:
            printInfo(f"[{toolName}] Downloading: {destinationFile} (Attempt {attempt}/{maxRetries})")
            
            urllib.request.urlretrieve(downloadUrl, destinationFile)
            printSuccess(f"[{toolName}] Download complete.")
            return True
            
        except (urllib.error.URLError, ValueError, ConnectionResetError) as error:
            printWarning(f"[{toolName}] Download failed: {error}")
            if attempt < maxRetries:
                time.sleep(5)
            else:
                printError(f"[{toolName}] Failed to download after {maxRetries} attempts.")
                sys.exit(1)

def verifySystemRequirements():
    printHeader("Running OS dependency checks")
    if sys.platform != 'win32':
        printError("This build script works exclusively for Windows.")
        sys.exit(1)
    currentWorkingDirectory = os.getcwd()
    try:
        testFilePath = os.path.join(currentWorkingDirectory, '.test.tmp')
        with open(testFilePath, 'w') as testFile: testFile.write("test")
        os.remove(testFilePath)
    except Exception:
        printError(f"Missing write permissions in {currentWorkingDirectory}")
        sys.exit(1)
    printSuccess("Requirements met.")

def getLatestGithubRelease(repo, suffix, fallback_url):
    url = f"https://api.github.com/repos/{repo}/releases/latest"
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=10) as response:
            data = json.loads(response.read().decode('utf-8'))
            for asset in data.get('assets',[]):
                if asset['name'].endswith(suffix):
                    return asset['browser_download_url']
    except Exception:
        pass
    return fallback_url

def check_git():
    if shutil.which("git"): return True
    if os.path.exists(r"C:\Program Files\Git\cmd\git.exe"):
        os.environ["PATH"] += os.pathsep + r"C:\Program Files\Git\cmd"
        return True
    return False

def check_cmake():
    if shutil.which("cmake"): return True
    if os.path.exists(r"C:\Program Files\CMake\bin\cmake.exe"):
        os.environ["PATH"] += os.pathsep + r"C:\Program Files\CMake\bin"
        return True
    return False

def get_vs_state():
    vsLocatorPath = os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
    if not os.path.exists(vsLocatorPath):
        return False, False, None, None
        
    cmd = [vsLocatorPath, "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json"]
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout_str = res.stdout.decode('utf-8', errors='ignore').strip()
        if not stdout_str:
            return False, False, None, None
            
        installations = json.loads(stdout_str)
        for inst in installations:
            path = inst.get("installationPath", "")
            version = inst.get("installationVersion", "")
            
            msvc_tools_dir = os.path.join(path, "VC", "Tools", "MSVC")
            if os.path.exists(path) and os.path.exists(msvc_tools_dir):
                has_mfc = False
                try:
                    for ver_folder in os.listdir(msvc_tools_dir):
                        if os.path.exists(os.path.join(msvc_tools_dir, ver_folder, "atlmfc", "include", "atlstr.h")):
                            has_mfc = True
                            break
                except Exception:
                    pass
                return True, has_mfc, path, version
    except Exception:
        pass
    return False, False, None, None

def check_vs():
    is_installed, has_mfc, _, _ = get_vs_state()
    return is_installed and has_mfc

def get_valid_dx_sdk():
    env_dir = os.environ.get("DXSDK_DIR", "")
    if env_dir and os.path.exists(os.path.join(env_dir, "Include", "d3d9.h")):
        return env_dir
        
    default_dir = r"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)"
    if os.path.exists(os.path.join(default_dir, "Include", "d3d9.h")):
        return default_dir
    return None

def check_dx():
    return get_valid_dx_sdk() is not None

TOOLS_CONFIG = {
    "Git": {
        "check": check_git,
        "url_func": lambda: getLatestGithubRelease("git-for-windows/git", "64-bit.exe", "https://github.com/git-for-windows/git/releases/download/v2.44.0.windows.1/Git-2.44.0-64-bit.exe"),
        "installer": "Git-Installer.exe",
        "install_cmd":["powershell", "-NoProfile", "-Command", "try { Start-Process -FilePath '{path}' -ArgumentList '/VERYSILENT /NORESTART /NOCANCEL /SP- /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS' -Wait -Verb RunAs -ErrorAction Stop } catch { exit 1 }"],
        "post_install": lambda: os.environ.update({"PATH": os.environ["PATH"] + os.pathsep + r"C:\Program Files\Git\cmd"})
    },
    "CMake": {
        "check": check_cmake,
        "url_func": lambda: getLatestGithubRelease("Kitware/CMake", "windows-x86_64.msi", "https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-windows-x86_64.msi"),
        "installer": "CMake-Installer.msi",
        "install_cmd":["powershell", "-NoProfile", "-Command", "try { Start-Process -FilePath 'msiexec.exe' -ArgumentList '/i \"{path}\" /qn /norestart ADD_CMAKE_TO_PATH=System' -Wait -Verb RunAs -ErrorAction Stop } catch { exit 1 }"],
        "post_install": lambda: os.environ.update({"PATH": os.environ["PATH"] + os.pathsep + r"C:\Program Files\CMake\bin"})
    },
    "VS Build Tools": {
        "check": check_vs,
        "url_func": lambda: "https://aka.ms/vs/17/release/vs_buildtools.exe",
        "installer": "vs_buildtools.exe",
        "install_cmd": [],
        "post_install": lambda: None
    },
    "DirectX SDK": {
        "check": check_dx,
        "url_func": lambda: "https://download.microsoft.com/download/A/E/7/AE743F1F-632B-4809-87A9-AA1BB3458E31/DXSDK_Jun10.exe",
        "installer": "DXSDK_Jun10.exe",
        "expected_size": 571,
        "install_cmd":["powershell", "-NoProfile", "-Command", "try { Start-Process -FilePath '{path}' -ArgumentList '/U' -Wait -Verb RunAs -ErrorAction Stop } catch { exit 1 }"],
        "post_install": lambda: None
    }
}

def resolveMissingPrerequisites():
    printHeader("Scanning for prerequisite build tools")
    
    missing_tools = [name for name, cfg in TOOLS_CONFIG.items() if not cfg["check"]()]
    if not missing_tools:
        printSuccess("All required build tools found.")
        return

    printWarning("Missing or corrupted tools detected: " + ", ".join(missing_tools))
    if input(f"\n{Colors.HEADER}Download and install? [Y/n]: {Colors.RESET}").strip().lower() not in['', 'y', 'yes']:
        sys.exit(1)

    for tool in missing_tools:
        config = TOOLS_CONFIG[tool]
        if not os.path.exists(config["installer"]):
            downloadWithRetry(config["url_func"](), config["installer"], tool, config.get("expected_size"))
        
        abs_installer = os.path.abspath(config["installer"]).replace("'", "''")
        
        if tool == "VS Build Tools":
            is_installed, has_mfc, vs_path, _ = get_vs_state()
            setup_exe = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe"
            
            if is_installed and not has_mfc and os.path.exists(setup_exe):
                printInfo(f"[{tool}] Using local VS Installer core to inject the ATLMFC package...")
                ps_args = f"modify --installPath `\"{vs_path}`\" --add Microsoft.VisualStudio.Component.VC.ATLMFC --passive --norestart"
                cmd = ["powershell", "-NoProfile", "-Command", f"Start-Process -FilePath '{setup_exe}' -ArgumentList \"{ps_args}\" -Wait -Verb RunAs"]
                runCommand(cmd, ignoreExitCode=True)
                
                printInfo("Waiting for VS Installer to finish writing ATLMFC files...")
                for _ in range(60): 
                    _, has_mfc_now, _, _ = get_vs_state()
                    if has_mfc_now:
                        printSuccess("ATLMFC files detected")
                        break
                    time.sleep(5)
            else:
                printInfo(f"[{tool}] Installing fresh C++ Build Tools...")
                ps_args = f"--passive --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.ATLMFC --includeRecommended"
                cmd = ["powershell", "-NoProfile", "-Command", f"Start-Process -FilePath '{abs_installer}' -ArgumentList \"{ps_args}\" -Wait -Verb RunAs"]
                runCommand(cmd, ignoreExitCode=True)
            
        else:
            printInfo(f"[{tool}] Installing... (Accept UAC Prompts. This may take a few minutes.)")
            res = runCommand([c.replace('{path}', abs_installer) for c in config["install_cmd"]], ignoreExitCode=True)
            if tool == "DirectX SDK" and res != 0:
                printWarning("[DirectX SDK] Installer threw a known bug error code (S1023). Bypassing safely.")
                
        config["post_install"]()

    if any(not TOOLS_CONFIG[t]["check"]() for t in missing_tools):
        printError("Some tools failed to verify after installation. Please reboot and try again.")
        sys.exit(1)
    printSuccess("All dependencies successfully installed!")

def manageRepository(repositoryUrl, targetDirectory):
    printHeader("Setting up the repository...")
    if os.path.exists(targetDirectory) and os.path.exists(os.path.join(targetDirectory, "CMakeLists.txt")):
        printInfo("Pulling latest updates...")
        runCommand(["git", "pull"], workingDirectory=targetDirectory)
        runCommand(["git", "submodule", "update", "--init", "--recursive"], workingDirectory=targetDirectory)
    else:
        if os.path.exists(targetDirectory): shutil.rmtree(targetDirectory, onerror=removeReadOnly)
        runCommand(["git", "clone", "--recursive", repositoryUrl, targetDirectory])

def get_valid_vs_instance():
    is_installed, has_mfc, path, version = get_vs_state()
    if is_installed and has_mfc and version:
        if version.startswith("17."): return "Visual Studio 17 2022", path
        if version.startswith("16."): return "Visual Studio 16 2019", path
        if version.startswith("15."): return "Visual Studio 15 2017", path
    return None, None

def buildAndInstall(targetDirectory):
    os.environ.pop("CC", None)
    os.environ.pop("CXX", None)
    os.environ.pop("CMAKE_GENERATOR", None)

    printHeader("Configuring CMake build")
    buildDirectory = "build"
    absoluteBuildDir = os.path.abspath(os.path.join(targetDirectory, buildDirectory))
    
    htmlHelpIncludePath = os.path.abspath(os.path.join(targetDirectory, "Extern", "Htmlhelp", "include"))
    htmlHelpLibraryPath = os.path.abspath(os.path.join(targetDirectory, "Extern", "Htmlhelp", "lib-x86", "Htmlhelp.Lib"))
    
    generator, instance_path = get_valid_vs_instance()
    
    if generator:
        printSuccess(f"Using CMake Generator: {generator}")
    else:
        printWarning("Could not determine specific CMake Generator. Automatically choosing the default.")
    
    dx_dir = get_valid_dx_sdk()
    if dx_dir:
        if not dx_dir.endswith('\\'):
            dx_dir += '\\'
        os.environ["DXSDK_DIR"] = dx_dir
    
    cmakeConfigCommand = [
        "cmake",
        "-B", buildDirectory,
        "-S", ".",
        "-A", "Win32",
        "-DCMAKE_INSTALL_PREFIX=install",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        f"-DHTML_HELP_INCLUDE_PATH={htmlHelpIncludePath}",
        f"-DHTML_HELP_LIBRARY={htmlHelpLibraryPath}",
        "-DHTML_HELP_COMPILER=hhc.exe"
    ]
    
    if generator:
        cmakeConfigCommand.insert(1, generator)
        cmakeConfigCommand.insert(1, "-G")
        
    if instance_path and generator:
        cmakeConfigCommand.append(f"-DCMAKE_GENERATOR_INSTANCE={instance_path}")
    
    runCommand(cmakeConfigCommand, workingDirectory=targetDirectory, quiet=True)

    printHeader("Compiling Project")
    cmakeBuildCommand = ["cmake", "--build", buildDirectory, "--config", "Release", "--parallel"]
    runCommand(cmakeBuildCommand, workingDirectory=targetDirectory, quiet=False)
    
    printHeader("Assembling Game Files")
    cmakeInstallCommand = ["cmake", "--install", buildDirectory, "--config", "Release"]
    runCommand(cmakeInstallCommand, workingDirectory=targetDirectory, quiet=True)
    
    gameInstallDirectory = os.path.abspath(os.path.join(targetDirectory, 'install'))
    
    ts_print("\n" + "="*60)
    ts_print(f"{Colors.SUCCESS}SUCCESS: Orbiter built successfully!{Colors.RESET}")
    ts_print("="*60)
    ts_print(f"{Colors.INFO}Build directory: {gameInstallDirectory}{Colors.RESET}")
    ts_print("="*60 + "\n")

def main():
    try:
        verifySystemRequirements()
        resolveMissingPrerequisites()
        manageRepository("https://github.com/orbitersim/orbiter.git", "OpenOrbiter")
        buildAndInstall("OpenOrbiter")
    except KeyboardInterrupt:
        printWarning("\nProcess interrupted by user.")
    except Exception as e:
        printError(f"\nUnexpected error: {e}")

if __name__ == "__main__":
    main()

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
    createUnverifiedHttpsContext = ssl._create_unverified_context
except AttributeError:
    pass
else:
    ssl._create_default_https_context = createUnverifiedHttpsContext

# ANSI escape
if sys.platform == 'win32':
    os.system("")

class colors:
    header = '\033[95m'    # Magenta
    info = '\033[96m'      # Cyan
    success = '\033[92m'   # Green
    warning = '\033[93m'   # Yellow
    error = '\033[91m'     # Red
    reset = '\033[0m'      # Reset

printLock = threading.Lock()

def tsPrint(*args, **kwargs):
    with printLock:
        print(*args, **kwargs)

def printHeader(text): tsPrint(f"\n{colors.header}=== {text} ==={colors.reset}")
def printSuccess(text): tsPrint(f"{colors.success}[SUCCESS] {text}{colors.reset}")
def printWarning(text): tsPrint(f"{colors.warning}[WARNING] {text}{colors.reset}")
def printError(text): tsPrint(f"{colors.error}[ERROR] {text}{colors.reset}")
def printInfo(text): tsPrint(f"{colors.info}{text}{colors.reset}")

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
                tsPrint(f"\n{colors.error}OUTPUT LOG{colors.reset}")
                if executionResult.stdout: tsPrint(executionResult.stdout)
                if executionResult.stderr: tsPrint(executionResult.stderr)
                tsPrint(f"{colors.error}------------------------------{colors.reset}")
                
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

def getLatestGithubRelease(repo, suffix, fallbackUrl):
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
    return fallbackUrl

def checkGit():
    if shutil.which("git"): return True
    if os.path.exists(r"C:\Program Files\Git\cmd\git.exe"):
        os.environ["PATH"] += os.pathsep + r"C:\Program Files\Git\cmd"
        return True
    return False

def checkCmake():
    if shutil.which("cmake"): return True
    if os.path.exists(r"C:\Program Files\CMake\bin\cmake.exe"):
        os.environ["PATH"] += os.pathsep + r"C:\Program Files\CMake\bin"
        return True
    return False

def getVsState():
    vsLocatorPath = os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
    if not os.path.exists(vsLocatorPath):
        return False, False, None, None
        
    cmd = [vsLocatorPath, "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json"]
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdoutStr = res.stdout.decode('utf-8', errors='ignore').strip()
        if not stdoutStr:
            return False, False, None, None
            
        installations = json.loads(stdoutStr)
        for inst in installations:
            path = inst.get("installationPath", "")
            version = inst.get("installationVersion", "")
            
            msvcToolsDir = os.path.join(path, "VC", "Tools", "MSVC")
            if os.path.exists(path) and os.path.exists(msvcToolsDir):
                hasMfc = False
                try:
                    for verFolder in os.listdir(msvcToolsDir):
                        if os.path.exists(os.path.join(msvcToolsDir, verFolder, "atlmfc", "include", "atlstr.h")):
                            hasMfc = True
                            break
                except Exception:
                    pass
                return True, hasMfc, path, version
    except Exception:
        pass
    return False, False, None, None

def checkVs():
    isInstalled, hasMfc, _, _ = getVsState()
    return isInstalled and hasMfc

def getValidDxSdk():
    envDir = os.environ.get("DXSDK_DIR", "")
    if envDir and os.path.exists(os.path.join(envDir, "Include", "d3d9.h")):
        return envDir
        
    defaultDir = r"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)"
    if os.path.exists(os.path.join(defaultDir, "Include", "d3d9.h")):
        return defaultDir
    return None

def checkDx():
    return getValidDxSdk() is not None

toolsConfig = {
    "Git": {
        "check": checkGit,
        "urlFunc": lambda: getLatestGithubRelease("git-for-windows/git", "64-bit.exe", "https://github.com/git-for-windows/git/releases/download/v2.44.0.windows.1/Git-2.44.0-64-bit.exe"),
        "installer": "Git-Installer.exe",
        "installCmd":["powershell", "-NoProfile", "-Command", "try { Start-Process -FilePath '{path}' -ArgumentList '/VERYSILENT /NORESTART /NOCANCEL /SP- /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS' -Wait -Verb RunAs -ErrorAction Stop } catch { exit 1 }"],
        "postInstall": lambda: os.environ.update({"PATH": os.environ["PATH"] + os.pathsep + r"C:\Program Files\Git\cmd"})
    },
    "CMake": {
        "check": checkCmake,
        "urlFunc": lambda: getLatestGithubRelease("Kitware/CMake", "windows-x86_64.msi", "https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-windows-x86_64.msi"),
        "installer": "CMake-Installer.msi",
        "installCmd":["powershell", "-NoProfile", "-Command", "try { Start-Process -FilePath 'msiexec.exe' -ArgumentList '/i \"{path}\" /qn /norestart ADD_CMAKE_TO_PATH=System' -Wait -Verb RunAs -ErrorAction Stop } catch { exit 1 }"],
        "postInstall": lambda: os.environ.update({"PATH": os.environ["PATH"] + os.pathsep + r"C:\Program Files\CMake\bin"})
    },
    "VS Build Tools": {
        "check": checkVs,
        "urlFunc": lambda: "https://aka.ms/vs/17/release/vs_buildtools.exe",
        "installer": "vs_buildtools.exe",
        "installCmd": [],
        "postInstall": lambda: None
    },
    "DirectX SDK": {
        "check": checkDx,
        "urlFunc": lambda: "https://download.microsoft.com/download/A/E/7/AE743F1F-632B-4809-87A9-AA1BB3458E31/DXSDK_Jun10.exe",
        "installer": "DXSDK_Jun10.exe",
        "expectedSize": 571,
        "installCmd":["powershell", "-NoProfile", "-Command", "try { Start-Process -FilePath '{path}' -ArgumentList '/U' -Wait -Verb RunAs -ErrorAction Stop } catch { exit 1 }"],
        "postInstall": lambda: None
    }
}

def resolveMissingPrerequisites():
    printHeader("Scanning for prerequisite build tools")
    
    missingTools = [name for name, cfg in toolsConfig.items() if not cfg["check"]()]
    if not missingTools:
        printSuccess("All required build tools found.")
        return

    printWarning("Missing or corrupted tools detected: " + ", ".join(missingTools))
    if input(f"\n{colors.header}Download and install? [Y/n]: {colors.reset}").strip().lower() not in['', 'y', 'yes']:
        sys.exit(1)

    for tool in missingTools:
        config = toolsConfig[tool]
        if not os.path.exists(config["installer"]):
            downloadWithRetry(config["urlFunc"](), config["installer"], tool, config.get("expectedSize"))
        
        absInstaller = os.path.abspath(config["installer"]).replace("'", "''")
        
        if tool == "VS Build Tools":
            isInstalled, hasMfc, vsPath, _ = getVsState()
            setupExe = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe"
            
            if isInstalled and not hasMfc and os.path.exists(setupExe):
                printInfo(f"[{tool}] Using local VS Installer core to inject the ATLMFC package...")
                psArgs = f"modify --installPath `\"{vsPath}`\" --add Microsoft.VisualStudio.Component.VC.ATLMFC --passive --norestart"
                cmd = ["powershell", "-NoProfile", "-Command", f"Start-Process -FilePath '{setupExe}' -ArgumentList \"{psArgs}\" -Wait -Verb RunAs"]
                runCommand(cmd, ignoreExitCode=True)
                
                printInfo("Waiting for VS Installer to finish writing ATLMFC files...")
                for _ in range(60): 
                    _, hasMfcNow, _, _ = getVsState()
                    if hasMfcNow:
                        printSuccess("ATLMFC files detected")
                        break
                    time.sleep(5)
            else:
                printInfo(f"[{tool}] Installing fresh C++ Build Tools...")
                psArgs = f"--passive --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.ATLMFC --includeRecommended"
                cmd = ["powershell", "-NoProfile", "-Command", f"Start-Process -FilePath '{absInstaller}' -ArgumentList \"{psArgs}\" -Wait -Verb RunAs"]
                runCommand(cmd, ignoreExitCode=True)
            
        else:
            printInfo(f"[{tool}] Installing... (Accept UAC Prompts. This may take a few minutes.)")
            res = runCommand([c.replace('{path}', absInstaller) for c in config["installCmd"]], ignoreExitCode=True)
            if tool == "DirectX SDK" and res != 0:
                printWarning("[DirectX SDK] Installer threw a known bug error code (S1023). Bypassing safely.")
                
        config["postInstall"]()

    if any(not toolsConfig[t]["check"]() for t in missingTools):
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

def getValidVsInstance():
    isInstalled, hasMfc, path, version = getVsState()
    if isInstalled and hasMfc and version:
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
    
    generator, instancePath = getValidVsInstance()
    
    if generator:
        printSuccess(f"Using CMake Generator: {generator}")
    else:
        printWarning("Could not determine specific CMake Generator. Automatically choosing the default.")
    
    dxDir = getValidDxSdk()
    if dxDir:
        if not dxDir.endswith('\\'):
            dxDir += '\\'
        os.environ["DXSDK_DIR"] = dxDir
    
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
    
    # Conditionally append specific generator if successfully detected
    if generator:
        cmakeConfigCommand.insert(1, generator)
        cmakeConfigCommand.insert(1, "-G")
        
    if instancePath and generator:
        cmakeConfigCommand.append(f"-DCMAKE_GENERATOR_INSTANCE={instancePath}")
    
    runCommand(cmakeConfigCommand, workingDirectory=targetDirectory, quiet=True)

    printHeader("Compiling Project")
    cmakeBuildCommand = ["cmake", "--build", buildDirectory, "--config", "Release", "--parallel"]
    runCommand(cmakeBuildCommand, workingDirectory=targetDirectory, quiet=False)
    
    printHeader("Assembling Game Files")
    cmakeInstallCommand = ["cmake", "--install", buildDirectory, "--config", "Release"]
    runCommand(cmakeInstallCommand, workingDirectory=targetDirectory, quiet=True)
    
    gameInstallDirectory = os.path.abspath(os.path.join(targetDirectory, 'install'))
    
    tsPrint("\n" + "="*60)
    tsPrint(f"{colors.success}SUCCESS: Orbiter built successfully!{colors.reset}")
    tsPrint("="*60)
    tsPrint(f"{colors.info}Build directory: {gameInstallDirectory}{colors.reset}")
    tsPrint("="*60 + "\n")

def main():
    try:
        verifySystemRequirements()
        resolveMissingPrerequisites()
        manageRepository("https://github.com/orbitersim/orbiter.git", "OpenOrbiter")
        buildAndInstall("OpenOrbiter")
    except KeyboardInterrupt:
        printWarning("\nProcess interrupted by user.")
    except Exception as error:
        printError(f"\nUnexpected error: {error}")

if __name__ == "__main__":
    main()

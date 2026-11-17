# U.ZU Engine: local development setup on Windows.
#
# Usage (PowerShell):
#   powershell -ExecutionPolicy Bypass -File setup.ps1 [-Path "D:\engine work\uzu開発"] [-SkipInstall] [-SkipBuild]
#
# 1. Installs Git, Python and the Visual Studio 2022 C++ Build Tools with winget
#    (skipped for tools already present, or with -SkipInstall).
# 2. Creates the development folder, clones ChinoMasato/godot and checks out ai-native
#    (or updates an existing clone).
# 3. Installs SCons and zstandard, builds the editor with unit tests and runs them.
#    Direct3D 12 and AccessKit (screen readers) are off by default, because they
#    need extra SDK downloads (misc/scripts/install_d3d12_sdk_windows.py and
#    misc/scripts/install_accesskit.py); pass -WithD3D12 / -WithAccessKit after
#    running those scripts. The editor then renders with Vulkan.
#
# The agent scripts in ai_loop/ (look.sh, engine.sh, screenshot.sh) are bash
# scripts for Linux; on Windows use them from WSL2 (Ubuntu), or use the cloud
# session. The Windows editor build is for working with a real GPU.

param(
	[string]$Path = (Join-Path $HOME "engine work\uzu開発"),
	[switch]$SkipInstall,
	[switch]$SkipBuild,
	[switch]$WithD3D12,
	[switch]$WithAccessKit
)

$ErrorActionPreference = "Stop"

function Have($command) { return [bool](Get-Command $command -ErrorAction SilentlyContinue) }

function Refresh-Path {
	$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}

if (-not $SkipInstall) {
	if (-not (Have winget)) { throw "winget is not available. Install 'App Installer' from the Microsoft Store, or install Git, Python 3 and Visual Studio 2022 Build Tools (C++) by hand and rerun with -SkipInstall." }
	if (-not (Have git)) {
		Write-Host "Installing Git..."
		winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements
	}
	if (-not (Have python)) {
		Write-Host "Installing Python..."
		winget install --id Python.Python.3.12 -e --accept-source-agreements --accept-package-agreements
	}
	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	$hasVC = (Test-Path $vswhere) -and (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
	if (-not $hasVC) {
		Write-Host "Installing Visual Studio 2022 Build Tools (C++). This takes a while..."
		winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements `
			--override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait"
	}
	Refresh-Path
}

foreach ($tool in "git", "python") {
	if (-not (Have $tool)) { throw "$tool was not found. Open a new PowerShell window (so PATH is updated) and rerun." }
}

New-Item -ItemType Directory -Force -Path $Path | Out-Null
$repo = Join-Path $Path "godot"
if (Test-Path (Join-Path $repo ".git")) {
	Write-Host "Updating $repo ..."
	git -C $repo fetch origin ai-native
	git -C $repo checkout ai-native
	git -C $repo pull --ff-only origin ai-native
} else {
	Write-Host "Cloning into $repo ..."
	git clone --branch ai-native https://github.com/ChinoMasato/godot.git $repo
}

python -m pip install --upgrade scons zstandard

if (-not $SkipBuild) {
	Push-Location $repo
	try {
		$jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
		Write-Host "Building the editor with unit tests (-j$jobs). The first build takes a while..."
		$options = @("platform=windows", "target=editor", "tests=yes", "-j", $jobs)
		if (-not $WithD3D12) { $options += "d3d12=no" }
		if (-not $WithAccessKit) { $options += "accesskit=no" }
		python -m SCons @options
		if ($LASTEXITCODE -ne 0) { throw "Build failed." }
		$exe = Get-ChildItem bin -Filter "godot.windows.editor*.console.exe" | Select-Object -First 1
		if ($exe) {
			Write-Host "Running unit tests..."
			& $exe.FullName --test
		}
		Write-Host ""
		Write-Host "Done. Editor: $(Join-Path $repo 'bin')"
	} finally {
		Pop-Location
	}
}

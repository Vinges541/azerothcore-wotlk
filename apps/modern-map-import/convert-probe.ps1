param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$Listfile,
    [string]$BundleDirectory = 'C:\Games\WoW-MapTools-20260906\bundle'
)

$ErrorActionPreference = 'Stop'
if ([IntPtr]::Size -ne 4) { throw 'Run with SysWOW64 Windows PowerShell (x86)' }
if (-not (Test-Path -LiteralPath $InputDirectory -PathType Container)) { throw 'Input directory missing' }
if (-not (Test-Path -LiteralPath $Listfile -PathType Leaf)) { throw 'Listfile missing' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output must not exist' }
$dlls = @(Get-ChildItem -LiteralPath $BundleDirectory -Recurse -Filter wxl-converter.dll)
if ($dlls.Count -ne 1) { throw 'Expected exactly one extracted converter DLL' }

# C ABI from wxl-cold-converter v0.0.1/converter/src/api/Api.hpp.
# This script calls the downloaded release DLL; it does not rebuild the converter.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace WxlProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct Settings {
        [MarshalAs(UnmanagedType.LPStr)] public string Input;
        [MarshalAs(UnmanagedType.LPStr)] public string Output;
        [MarshalAs(UnmanagedType.LPStr)] public string Listfile;
        public uint Threads;
        public int WantM2, WantWmo, WantAdt, WantBlp;
        public int AdtHighResHoles, AdtFixWater, AdtShowDoodads, AdtShowWmoObjects;
        public int AdtPreserveWmoScale, AdtPackExtraTextureLayers, AdtUvScaleTable, AdtHeightBlendTable;
        public int AdtPreserveAreaId, AdtPreserveGroundEffectId, AdtFixWdtBigAlpha, AdtConvertWdl;
        public int WmoShowDoodads, WmoIncludePortals, WmoIncludeLights, WmoIncludeLiquid;
        public int WmoIncludeCollision, WmoNeutralizeVertexColors;
        public int M2AnimationFix, M2RibbonCompact, M2ShadowSwingFix, M2UnwrapAnim;
        public uint BlpDefaultMaxEdge;
        public IntPtr BlpPathCaps;
        public uint BlpPathCapCount;
        public int BlpExemptSideMapsFromPathCaps, BlpTranscodeToDxt5;
    }
    public static class Native {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool SetDllDirectory(string path);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void LogCallback(IntPtr message);
        public static readonly LogCallback Logger = Log;
        private static void Log(IntPtr message) { Console.WriteLine(Marshal.PtrToStringAnsi(message)); }
        [DllImport("wxl-converter.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern void WxlSetCallbacks(LogCallback log, IntPtr scan, IntPtr progress);
        [DllImport("wxl-converter.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern int WxlRun(ref Settings settings);
    }
}
'@

if ([Runtime.InteropServices.Marshal]::SizeOf([type][WxlProbe.Settings]) -ne 140) {
    throw 'Unexpected settings ABI size'
}
if (-not [WxlProbe.Native]::SetDllDirectory($dlls[0].DirectoryName)) { throw 'Cannot set DLL directory' }
$settings = New-Object WxlProbe.Settings
$settings.Input = $InputDirectory
$settings.Output = $OutputDirectory
$settings.Listfile = $Listfile
$settings.Threads = 2
foreach ($field in @(
    'WantM2', 'WantWmo', 'WantAdt', 'WantBlp',
    'AdtHighResHoles', 'AdtFixWater', 'AdtShowDoodads', 'AdtShowWmoObjects',
    'AdtPreserveWmoScale', 'AdtPackExtraTextureLayers', 'AdtUvScaleTable', 'AdtHeightBlendTable',
    'AdtPreserveAreaId', 'AdtPreserveGroundEffectId', 'AdtFixWdtBigAlpha', 'AdtConvertWdl',
    'WmoShowDoodads', 'WmoIncludePortals', 'WmoIncludeLights', 'WmoIncludeLiquid', 'WmoIncludeCollision',
    'M2AnimationFix', 'M2RibbonCompact', 'M2ShadowSwingFix', 'M2UnwrapAnim',
    'BlpExemptSideMapsFromPathCaps', 'BlpTranscodeToDxt5'
)) { $settings.$field = 1 }
$settings.BlpDefaultMaxEdge = 1024
[WxlProbe.Native]::WxlSetCallbacks([WxlProbe.Native]::Logger, [IntPtr]::Zero, [IntPtr]::Zero)
$result = [WxlProbe.Native]::WxlRun([ref]$settings)
Write-Output "WxlRun result=$result"
if ($result -ne 0) { throw "Conversion incomplete (result $result); inspect output before use" }

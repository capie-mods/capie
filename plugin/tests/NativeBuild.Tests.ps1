<#
.SYNOPSIS
    Offline Pester 3.4 tests for plugin/NativeBuild.psm1 (no compiler, no game process needed).

.DESCRIPTION
    Every adapter (game processes, vswhere, vcvars, tool execution) is replaced, so the
    tests only check the decisions and the exact compiler command lines. The expected
    command lines are the ones the build scripts used before the shared module existed.
        Invoke-Pester -Script .\plugin\tests\NativeBuild.Tests.ps1
#>

$pluginDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $pluginDir 'NativeBuild.psm1') -Force
Set-StrictMode -Version Latest
$probeFlags = @('/nologo', '/O2', '/MT', '/W4', '/WX', '/utf-8', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX')
$minHookObjects = @('buffer.obj', 'hook.obj', 'trampoline.obj', 'hde64.obj')

Describe 'NativeBuild command lines (golden values from the original scripts)' {
    It 'probe DLL with MinHook' {
        $dllArgs = @{ Flags = (Get-NbFlagSet Probe); Source = 'C:\p\x.cpp'; IncludeDir = 'C:\mh\include'; LinkObject = $minHookObjects; OutFile = 'x.dll' }
        $a = New-NbCompilerArgument @dllArgs -Dll
        ($a -join ' ') | Should BeExactly (($probeFlags + @('/std:c++20', '/EHsc', '/LD', 'C:\p\x.cpp', '/IC:\mh\include') + $minHookObjects +
                @('/link', 'bcrypt.lib', '/OUT:x.dll')) -join ' ')
    }
    It 'probe DLL with an assembly listing' {
        $a = New-NbCompilerArgument -Flags (Get-NbFlagSet Probe) -ExtraFlags @('/FAs', '/Fax.asm') -Dll -Source 'x.cpp' -IncludeDir 'I' `
            -LinkObject $minHookObjects -OutFile 'x.dll'
        ($a -join ' ') | Should Match '/EHsc /FAs /Fax\.asm /LD x\.cpp /II buffer\.obj'
    }
    It 'country build: /WX after the common set, protocol test without libraries' {
        (Get-NbFlagSet Country) -join ' ' | Should BeExactly '/nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /WX'
        $a = New-NbCompilerArgument -Flags (Get-NbFlagSet Country) -PreFlags '/UNDEBUG' -Source 't.cpp' -LinkLibrary @() -OutFile 'protocol_test.exe'
        $expected = '/nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /WX /UNDEBUG /std:c++20 /EHsc t.cpp /link /OUT:protocol_test.exe'
        ($a -join ' ') | Should BeExactly $expected
    }
    It 'never produces an empty argument (regression: $x = if (...) { @() } binds as one empty string)' {
        $a = New-NbCompilerArgument -Flags (Get-NbFlagSet Probe) -Source 'x.cpp' -OutFile 'x.exe'
        @($a | Where-Object { $_ -eq '' }).Count | Should Be 0
        $threw = $false
        try { [void](New-NbCompilerArgument -Flags @('/nologo', '') -Source 'x.cpp' -OutFile 'x.exe') } catch { $threw = $true }
        $threw | Should Be $true
    }
}

Describe 'NativeBuild adapters' {
    It 'a running game that loaded the DLL blocks the build' {
        $old = Set-NbAdapter GetGameProcess { @([pscustomobject]@{ Modules = @([pscustomobject]@{ FileName = 'C:\b\x.dll' }) }) }
        try {
            $threw = $false
            try { Assert-NbModuleNotLoaded -ModulePath 'C:\b\x.dll' -Message 'loaded' } catch { $threw = $_.Exception.Message -eq 'loaded' }
            $threw | Should Be $true
            Assert-NbModuleNotLoaded -ModulePath 'C:\b\other.dll' -Message 'loaded'
        } finally { [void](Set-NbAdapter GetGameProcess $old) }
    }
    It 'Visual Studio: vswhere result is used, missing VS and failing vcvars throw, environment is imported' {
        $oldFind = Set-NbAdapter FindVisualStudio { 'C:\VS' }
        $oldRead = Set-NbAdapter ReadVcVarsEnvironment {
            param($path)
            @{ ExitCode = 0; Lines = @("RA_NB_TEST_VAR=$path", 'not a variable line') }
        }
        try {
            Import-NbVisualStudioEnvironment | Should Be 'C:\VS'
            $env:RA_NB_TEST_VAR | Should Be 'C:\VS\VC\Auxiliary\Build\vcvars64.bat'
            [void](Set-NbAdapter FindVisualStudio { '' })
            $threw = $false
            try { [void](Import-NbVisualStudioEnvironment) } catch { $threw = $_.Exception.Message -match 'required' }
            $threw | Should Be $true
            [void](Set-NbAdapter ReadVcVarsEnvironment { @{ ExitCode = 1; Lines = @() } })
            $threw = $false
            try { [void](Import-NbVisualStudioEnvironment -VisualStudioDir 'C:\VS') } catch { $threw = $_.Exception.Message -eq 'vcvars64 failed' }
            $threw | Should Be $true
        } finally {
            [void](Set-NbAdapter FindVisualStudio $oldFind)
            [void](Set-NbAdapter ReadVcVarsEnvironment $oldRead)
            Remove-Item Env:\RA_NB_TEST_VAR -ErrorAction SilentlyContinue
        }
    }
    It 'tool adapter: a non-zero exit code becomes the given error' {
        $old = Set-NbAdapter InvokeTool { 3 }
        try {
            $threw = $false
            try { Invoke-NbTool -FilePath 'cl.exe' -FailureMessage 'boom' } catch { $threw = $_.Exception.Message -eq 'boom' }
            $threw | Should Be $true
        } finally { [void](Set-NbAdapter InvokeTool $old) }
    }
    It 'Invoke-NbProbeBuild runs MinHook, every step and each test in order, then copies the licence' {
        $root = Join-Path ([IO.Path]::GetTempPath()) ('ra-nb-test-' + [guid]::NewGuid().ToString('N'))
        $probeRoot = Join-Path $root 'plugin\probes'
        $minHookDir = Join-Path $root 'minhook'
        New-Item -ItemType Directory -Force -Path $probeRoot, (Join-Path $minHookDir 'include') | Out-Null
        Set-Content -LiteralPath (Join-Path $minHookDir 'include\MinHook.h') -Value '//'
        Set-Content -LiteralPath (Join-Path $minHookDir 'LICENSE.txt') -Value 'license'
        $script:calls = New-Object System.Collections.ArrayList
        $saved = @{
            InvokeTool            = Set-NbAdapter InvokeTool { param($f, $a) [void]$script:calls.Add($f + ' ' + ($a -join ' ')); 0 }
            GetGameProcess        = Set-NbAdapter GetGameProcess { @() }
            FindVisualStudio      = Set-NbAdapter FindVisualStudio { 'C:\VS' }
            ReadVcVarsEnvironment = Set-NbAdapter ReadVcVarsEnvironment { @{ ExitCode = 0; Lines = @() } }
        }
        try {
            $steps = @(
                @{ Kind = 'HookedDll'; Source = 'p.cpp'; Out = 'p.dll'; Failure = 'f1' },
                @{ Kind = 'Injector'; Source = 'i.cpp'; Out = 'i.exe'; Failure = 'f2' },
                @{ Kind = 'HookedTest'; Source = 't.cpp'; Out = 't.exe'; Failure = 'f3'; RunFailure = 'r3' }
            )
            $buildArgs = @{
                ProbeRoot = $probeRoot; OutputName = 'probe-x'; DllName = 'p.dll'; LoadedMessage = 'loaded'; MinHookDir = $minHookDir
                VisualStudioDir = ''; Steps = $steps; DoneMessage = 'Built: {0}'
            }
            $message = Invoke-NbProbeBuild @buildArgs
            $out = Join-Path $root 'plugin\build\probe-x'
            $message | Should Be ('Built: ' + (Join-Path $out 'p.dll'))
            $script:calls.Count | Should Be 5
            $script:calls[0] | Should Match '^cl\.exe /nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /c /TC .*buffer\.c .*hde64\.c /I'
            $script:calls[1] | Should Match ('^cl\.exe .* /LD ' + [regex]::Escape((Join-Path $probeRoot 'p.cpp')))
            $script:calls[2] | Should Match '/OUT:i\.exe$'
            $script:calls[3] | Should Match '/UNDEBUG /std:c\+\+20 /EHsc .*t\.cpp'
            $script:calls[4] | Should Be '.\t.exe '
            # No empty argument anywhere (it would show up as a double space).
            foreach ($c in $script:calls[0..3]) { $c | Should Not Match '  ' }
            Test-Path -LiteralPath (Join-Path $out 'LICENSE.MinHook.txt') | Should Be $true
        } finally {
            foreach ($k in $saved.Keys) { [void](Set-NbAdapter $k $saved[$k]) }
            Remove-Item -LiteralPath $root -Recurse -Force
        }
    }
}

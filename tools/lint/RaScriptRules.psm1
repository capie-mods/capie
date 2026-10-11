<#
.SYNOPSIS
    Project-specific PSScriptAnalyzer rules ("contract" rules) for this repository.

.DESCRIPTION
    PowerShell has no static type checker. These rules give the closest practical
    equivalent and enforce the documentation contract of the project:

      RaFunctionHelp    every function has comment-based help with .SYNOPSIS,
                        .DESCRIPTION, one .PARAMETER per declared parameter,
                        .OUTPUTS and at least one .EXAMPLE.
      RaCmdletBinding   every function is an advanced function ([CmdletBinding()]).
      RaOutputType      every function declares [OutputType(...)].
      RaTypedParameter  every parameter has a type constraint.
      RaStrictMode      every script with a script-level param block calls
                        Set-StrictMode -Version Latest somewhere in the file.

    Load with: Invoke-ScriptAnalyzer -CustomRulePath tools/lint/RaScriptRules.psm1
    (tools/lint-powershell.ps1 does that). A finding can be suppressed like any
    other rule, e.g. [Diagnostics.CodeAnalysis.SuppressMessageAttribute('RaStrictMode', '')],
    and every suppression must carry a Justification.
#>
Set-StrictMode -Version Latest

$script:RaRuleSeverity = 'Warning'
$script:RaRequiredStrictModeVersion = 'Latest'

function New-RaDiagnostic {
    <#
    .SYNOPSIS
        Creates one PSScriptAnalyzer diagnostic record.
    .DESCRIPTION
        Small factory so that every rule reports findings with the same severity and shape.
    .PARAMETER RuleName
        Name reported in the RuleName column and used for SuppressMessageAttribute.
    .PARAMETER Message
        Human-readable description of the finding.
    .PARAMETER Extent
        Source extent the finding points at.
    .OUTPUTS
        Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord
    .EXAMPLE
        New-RaDiagnostic -RuleName 'RaOutputType' -Message 'missing' -Extent $ast.Extent
    #>
    [CmdletBinding()]
    [OutputType([Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord])]
    param(
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$RuleName,
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Message,
        [Parameter(Mandatory)][ValidateNotNull()][System.Management.Automation.Language.IScriptExtent]$Extent
    )
    return [Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord]@{
        Message  = $Message
        Extent   = $Extent
        RuleName = $RuleName
        Severity = $script:RaRuleSeverity
    }
}

function Get-RaFunctionParameter {
    <#
    .SYNOPSIS
        Returns the parameters of a function, wherever they are declared.
    .DESCRIPTION
        Supports both the param() block and the "function f($a)" form.
    .PARAMETER FunctionAst
        The function definition to inspect.
    .OUTPUTS
        System.Management.Automation.Language.ParameterAst
    .EXAMPLE
        Get-RaFunctionParameter -FunctionAst $fn
    #>
    [CmdletBinding()]
    [OutputType([System.Management.Automation.Language.ParameterAst], [object[]])]
    param([Parameter(Mandatory)][System.Management.Automation.Language.FunctionDefinitionAst]$FunctionAst)
    if ($FunctionAst.Body.ParamBlock) { return @($FunctionAst.Body.ParamBlock.Parameters) }
    if ($FunctionAst.Parameters) { return @($FunctionAst.Parameters) }
    return @()
}

function Test-RaHasAttribute {
    <#
    .SYNOPSIS
        Tests whether a function's param block carries an attribute of the given type name.
    .DESCRIPTION
        Compares the attribute type name case-insensitively, with or without the
        "Attribute" suffix.
    .PARAMETER FunctionAst
        The function definition to inspect.
    .PARAMETER Name
        Attribute name, e.g. CmdletBinding or OutputType.
    .OUTPUTS
        System.Boolean
    .EXAMPLE
        Test-RaHasAttribute -FunctionAst $fn -Name 'OutputType'
    #>
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory)][System.Management.Automation.Language.FunctionDefinitionAst]$FunctionAst,
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Name
    )
    $block = $FunctionAst.Body.ParamBlock
    if (-not $block) { return $false }
    foreach ($attribute in $block.Attributes) {
        $typeName = $attribute.TypeName.Name
        if ($typeName -ieq $Name -or $typeName -ieq ($Name + 'Attribute')) { return $true }
    }
    return $false
}

function Measure-RaFunctionContract {
    <#
    .SYNOPSIS
        PSScriptAnalyzer rule: help, CmdletBinding, OutputType and typed parameters for every function.
    .DESCRIPTION
        Runs once per file (on the root script block) and inspects every function
        definition in it, including nested ones.
    .PARAMETER ScriptBlockAst
        Script block supplied by PSScriptAnalyzer.
    .OUTPUTS
        Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord[]
    .EXAMPLE
        Invoke-ScriptAnalyzer -Path x.ps1 -CustomRulePath tools/lint/RaScriptRules.psm1
    #>
    [CmdletBinding()]
    [OutputType([Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord[]], [object[]])]
    param([Parameter(Mandatory)][ValidateNotNull()][System.Management.Automation.Language.ScriptBlockAst]$ScriptBlockAst)
    if ($null -ne $ScriptBlockAst.Parent) { return @() }
    $results = New-Object System.Collections.Generic.List[object]
    $functions = $ScriptBlockAst.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)
    foreach ($fn in $functions) {
        $name = $fn.Name
        $extent = $fn.Extent
        $parameters = @(Get-RaFunctionParameter -FunctionAst $fn)
        $help = $fn.GetHelpContent()
        if (-not $help) {
            $results.Add((New-RaDiagnostic -RuleName 'RaFunctionHelp' -Extent $extent -Message "Function '$name' has no comment-based help."))
        } else {
            $missing = @()
            if ([string]::IsNullOrWhiteSpace($help.Synopsis)) { $missing += '.SYNOPSIS' }
            if ([string]::IsNullOrWhiteSpace($help.Description)) { $missing += '.DESCRIPTION' }
            if ($help.Outputs.Count -eq 0) { $missing += '.OUTPUTS' }
            if ($help.Examples.Count -eq 0) { $missing += '.EXAMPLE' }
            foreach ($p in $parameters) {
                $pname = $p.Name.VariablePath.UserPath
                if (-not $help.Parameters.ContainsKey($pname.ToUpperInvariant())) { $missing += ".PARAMETER $pname" }
            }
            if ($missing.Count -gt 0) {
                $message = "Function '$name' help is missing: " + ($missing -join ', ')
                $results.Add((New-RaDiagnostic -RuleName 'RaFunctionHelp' -Extent $extent -Message $message))
            }
        }
        if (-not (Test-RaHasAttribute -FunctionAst $fn -Name 'CmdletBinding')) {
            $message = "Function '$name' is not an advanced function; add [CmdletBinding()]."
            $results.Add((New-RaDiagnostic -RuleName 'RaCmdletBinding' -Extent $extent -Message $message))
        }
        if (-not (Test-RaHasAttribute -FunctionAst $fn -Name 'OutputType')) {
            $results.Add((New-RaDiagnostic -RuleName 'RaOutputType' -Extent $extent -Message "Function '$name' does not declare [OutputType()]."))
        }
        foreach ($p in $parameters) {
            $typed = @($p.Attributes | Where-Object { $_ -is [System.Management.Automation.Language.TypeConstraintAst] }).Count -gt 0
            if (-not $typed) {
                $message = "Parameter '$($p.Name.VariablePath.UserPath)' of '$name' has no type constraint."
                $results.Add((New-RaDiagnostic -RuleName 'RaTypedParameter' -Extent $p.Extent -Message $message))
            }
        }
    }
    return $results.ToArray()
}

function Measure-RaStrictMode {
    <#
    .SYNOPSIS
        PSScriptAnalyzer rule: scripts with a param block must enable Set-StrictMode -Version Latest.
    .DESCRIPTION
        Fires once per file when the root script block has a param block and the file
        contains no "Set-StrictMode -Version Latest" call. A script that must not change
        the caller's strict mode at top level (irm | iex) can enable it inside its main
        function; the call can be anywhere in the file.
    .PARAMETER ScriptBlockAst
        Script block supplied by PSScriptAnalyzer.
    .OUTPUTS
        Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord[]
    .EXAMPLE
        Invoke-ScriptAnalyzer -Path x.ps1 -CustomRulePath tools/lint/RaScriptRules.psm1
    #>
    [CmdletBinding()]
    [OutputType([Microsoft.Windows.PowerShell.ScriptAnalyzer.Generic.DiagnosticRecord[]], [object[]])]
    param([Parameter(Mandatory)][ValidateNotNull()][System.Management.Automation.Language.ScriptBlockAst]$ScriptBlockAst)
    if ($null -ne $ScriptBlockAst.Parent -or $null -eq $ScriptBlockAst.ParamBlock) { return @() }
    $calls = $ScriptBlockAst.FindAll({
            param($node)
            $node -is [System.Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ieq 'Set-StrictMode'
        }, $true)
    foreach ($call in $calls) {
        $elements = @($call.CommandElements)
        for ($i = 1; $i -lt $elements.Count - 1; $i++) {
            if ($elements[$i] -is [System.Management.Automation.Language.CommandParameterAst] -and
                $elements[$i].ParameterName -ieq 'Version' -and
                $elements[$i + 1].Extent.Text -ieq $script:RaRequiredStrictModeVersion) { return @() }
        }
    }
    $message = 'Script does not call Set-StrictMode -Version Latest.'
    return @(New-RaDiagnostic -RuleName 'RaStrictMode' -Extent $ScriptBlockAst.ParamBlock.Extent -Message $message)
}

Export-ModuleMember -Function 'Measure-RaFunctionContract', 'Measure-RaStrictMode'

# PSScriptAnalyzer settings for every PowerShell file in this repository.
# Run: tools/lint-powershell.ps1 (adds the project rules in tools/lint/RaScriptRules.psm1).
#
# Policy: all default rules at all severities (Error, Warning, Information), plus every
# opt-in rule that makes sense for scripts that must run on Windows PowerShell 5.1 AND
# PowerShell 7. Each exclusion below states why. A suppression in code must use
# [Diagnostics.CodeAnalysis.SuppressMessageAttribute(...)] with a Justification.
@{
    Severity     = @('Error', 'Warning', 'Information')

    IncludeDefaultRules = $true

    ExcludeRules = @(
        # The bootstrap and the build scripts implement their own explicit dry-run
        # (-DryRun, alias -WhatIf, PLAN log lines). Internal helpers are not exported
        # cmdlets; adding SupportsShouldProcess would create a second, conflicting what-if
        # mechanism and $WhatIfPreference would silently leak into Copy-Item/Remove-Item.
        'PSUseShouldProcessForStateChangingFunctions',

        # These scripts need FullLanguage by design (Add-Type for ZIP/WinForms, COM for
        # .lnk shortcuts, .NET crypto and file APIs). They are not meant to run under
        # Constrained Language Mode, so this rule would flag every line of real work.
        'PSUseConstrainedLanguageMode',

        # DSC rules: there are no DSC resources in this repository.
        'PSDSCDscExamplesPresent',
        'PSDSCDscTestsPresent',
        'PSDSCReturnCorrectTypesForDSCFunctions',
        'PSDSCUseIdenticalMandatoryParametersForDSC',
        'PSDSCUseIdenticalParametersForDSC',
        'PSDSCStandardDSCFunctionsInResource',
        'PSDSCUseVerboseMessageInDSCResource'
    )

    Rules = @{
        PSPlaceOpenBrace = @{
            Enable             = $true
            OnSameLine         = $true
            NewLineAfter       = $true
            IgnoreOneLineBlock = $true
        }
        PSPlaceCloseBrace = @{
            Enable             = $true
            # $false allows the "} else {" / "} catch {" style used everywhere.
            NewLineAfter       = $false
            IgnoreOneLineBlock = $true
            NoEmptyLineBefore  = $true
        }
        PSUseConsistentIndentation = @{
            Enable              = $true
            Kind                = 'space'
            IndentationSize     = 4
            PipelineIndentation = 'IncreaseIndentationForFirstPipeline'
        }
        PSUseConsistentWhitespace = @{
            Enable                                  = $true
            CheckInnerBrace                         = $true
            CheckOpenBrace                          = $true
            CheckOpenParen                          = $true
            CheckOperator                           = $true
            CheckPipe                               = $true
            CheckPipeForRedundantWhitespace         = $true
            CheckSeparator                          = $true
            CheckParameter                          = $true
            # Aligned "Key = value" columns in hashtables are kept for readability.
            IgnoreAssignmentOperatorInsideHashTable = $true
        }
        PSUseCorrectCasing = @{
            Enable = $true
        }
        PSAvoidLongLines = @{
            Enable            = $true
            # 160 instead of the 120 default: Windows paths, compiler command lines and
            # bilingual message tables are long by nature; beyond 160 they are split.
            MaximumLineLength = 160
        }
        PSAvoidSemicolonsAsLineTerminators = @{
            Enable = $true
        }
        PSAvoidExclaimOperator = @{
            Enable = $true
        }
        PSAvoidUsingDoubleQuotesForConstantString = @{
            Enable = $true
        }
        PSProvideCommentHelp = @{
            Enable                  = $true
            ExportedOnly            = $false
            BlockComment            = $true
            VSCodeSnippetCorrection = $false
            Placement               = 'begin'
        }
        PSUseCompatibleSyntax = @{
            Enable         = $true
            TargetVersions = @('5.1', '7.0')
        }
        PSUseCompatibleCommands = @{
            Enable         = $true
            TargetProfiles = @(
                'win-8_x64_10.0.17763.0_5.1.17763.316_x64_4.0.30319.42000_framework',
                'win-8_x64_10.0.17763.0_7.0.0_x64_3.1.2_core'
            )
        }
        PSUseCompatibleTypes = @{
            Enable         = $true
            TargetProfiles = @(
                'win-8_x64_10.0.17763.0_5.1.17763.316_x64_4.0.30319.42000_framework',
                'win-8_x64_10.0.17763.0_7.0.0_x64_3.1.2_core'
            )
        }
    }
}

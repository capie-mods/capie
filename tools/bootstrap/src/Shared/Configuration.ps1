# ---------------------------------------------------------------------------
# Release configuration: the ONLY values that change for a public release.
# ---------------------------------------------------------------------------

# GitHub "owner/repo". Release downloads and the (on-demand only) update check derive from it.
$script:RaGitHubRepo = 'capie-mods/capie'

# RSA public key (XML, >= 2048 bit) that signs release-manifest.json of every later release.
# Placeholder until the maintainer generates the key (tools/bootstrap/sign-release-manifest.ps1).
$script:RaUpdatePublicKeyXml = '<RSAKeyValue><Modulus>21/rZrMx9hLPYwh/QMiCnXaOUebJdj+aLFxd/vpqfcDsrczFXN6j8JA/9Iuch3Nc5aFr/KRMhtlGsPqf6ov/IseZUd//Q5snHolFyZ9QI78/k8lEGZ4rkojPZoqg8SQh5DdNYA0UHL3D/uI6HL26ZwfV7v3oAlcTUXFdW1quGvtxFlDNDFCvqHMl+Hc7BpOwQQXG4ChS08UHSYsTaBr0U7nFC734w4wqgae3ahZJBFDmubfkPiPZd7x+NExEihOLbwrzHc1NegtSEBlOcOWxwBlLXp49JitQttrJyA+2/fuWFACMqynMsSWtUnOslslULb0vcAbJDV9RtdS4qLZsAZVfJjbuk0+kfqN7ztOl2biXKCu7OlzvP4t9hfA5DCoqyP3PGqZTA8nFqXkIU5USKRg75cwQ/j7YczH2SBQpNghivWghCQMUVUgheL1ETk6BZCY+oJy1RIEuWkb2W002RsmmX/4R69YdCDAazCEX6sTWqALFboMLGd+5AMQRu6ZV</Modulus><Exponent>AQAB</Exponent></RSAKeyValue>'

# Pinned release manifest. The ZIP hash is checked BEFORE extraction and every
# extracted file is checked against its own pinned hash; any mismatch, missing or
# extra file aborts the install (fail-closed). File hashes below are the current
# reviewed launcher-candidate bundle; ZipSha256 must be filled when the release ZIP
# is built. Until then the installer refuses to install anything.
$script:RaPinnedRelease = [ordered]@{
    Version             = '1.5.0'
    ZipName             = 'CAPIE-1.5.0-win64.zip'
    ZipSha256           = 'C84B5E7C0C9F64BFC9C691A8F0DDA5F2CABC8745BCD144050DCECF56EE33D8C3'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe'                                = '610E61EE010FFCD40F2BF9DEC1774BDEF2D38164CE7274D548B50E3BDFCAF14C'
        'LICENSE.MinHook.txt'                                       = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/capie_core.dll'                                    = '7A1C946F8A15F6120A0232453E3013B9F854EC2A6B526D0AB9C86F19959D0DE1'
        'runtime/capie_editor.dll'                                  = '0DAD2732552305F43E1E87FB6E10E6A5B473E683EA27F1E82D0EBD9B8934D656'
        'runtime/capie_injector.exe'                                = 'EB5E9FF5F9B944B55CA5EB4B343D18D507584284FB60F5F119A8BE8843484C25'
        'proxy/version.dll'                                         = '940FCD0A418078FBB3AF2ACD013D73C718EB5B03605064BADF4A2ECF202A6A21'
        'mod/capie.mod'                                             = '30713E068E274A81777ABECEF59C022F7526F14268B3696674BF8C4A9790AD47'
        'mod/capie/descriptor.mod'                                  = '07F8081AF1353805C558F58792015FC85E6C4CBE87710C190DAF8C48DB759D58'
        'mod/capie/gfx/interface/capie_menu_mark.dds'               = '6842E6D83D773648EE7A5878EA5B0A2E77C66353C926406CFE95B23C88B2B522'
        'mod/capie/interface/capie_menu.gfx'                        = '392A554D2286B5C81591946E4E0AA439FCC0D3A1D5471F56D472B656B242E695'
        'mod/capie/interface/frontendmainview.gui'                  = '4456020EE6FEB4F88A41AA9888FB22C538F58F5F0CD285C91529B768AE2BB1CC'
        'mod/capie/localisation/english/capie_switch_l_english.yml' = 'EE94308AB621CC46BC70AD066F1E223AF2071FA9F4A3728797ABD04AF23ABC3F'
        'mod/capie/localisation/polish/capie_switch_l_polish.yml'   = 'E59AF5200E9D539A9AD36AC8A6EDEDD1D589C55FC176236B9498DB92C0182CA9'
        'mod/capie/thumbnail.png'                                   = 'D4BB22A36F572D354E49DCFB54DCF03F03BF844517F261FF2B9BC2D94CFD66FE'
    }
    # hoi4.exe builds this release's DLLs support (for -CheckUpdate messages; the launcher enforces it).
    SupportedGameSha256 = @('7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5')
}

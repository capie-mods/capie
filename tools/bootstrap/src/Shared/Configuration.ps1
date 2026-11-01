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
#
# Never edit this block by hand. The release step writes all of it, SupportedGameSha256
# included, from the bundle it just built and signed (tools/release/release.py build
# --write-pins; SupportedGameSha256 = game_exe_sha256 of plugin/capie/src/core/game_build.hpp,
# which the build step checks inside every shipped binary), then rebuilds install.ps1.
# See docs/contributing/release-script.md, "Installer pins".
#
# The values below are still those of CAPIE 1.5.0 for HOI4 1.19.3.0 (hoi4.exe 7DC947BE...).
# On HOI4 1.19.4.0 an install of this pinned release therefore ends with the clear
# "does NOT support your Hearts of Iron IV build" notice (and the release page and update
# command), never a silent success. CAPIE 1.5.1 replaces them when it is released.
$script:RaPinnedRelease = [ordered]@{
    Version             = '1.6.0'
    ZipName             = 'CAPIE-1.6.0-win64.zip'
    ZipSha256           = '351923ACE07DF0C204226730B03C3DFD7D54022819D9D1C022AA591DEB753C79'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe'                                = 'AB1DD952084DADA498C6857EA535E6E5D4860448C8351C9DD8316A1E37DBF5CD'
        'LICENSE.MinHook.txt'                                       = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/capie_core.dll'                                    = '89082E238D3E759BBDBC4811984D7455E94E2CF1607AA87209246E63F5E2DCFE'
        'runtime/capie_editor.dll'                                  = '57045C51E9DBAEB753E31F5CB591BA53648C6C4EB86B37A025CEB051F1AA277E'
        'runtime/capie_injector.exe'                                = '3F6F735B8202090F465C7C945C36E633CE954C737E1AC7DF8F6B4C3F48E8E04E'
        'proxy/version.dll'                                         = 'D019DE82A0FC7585927858299C86897B75FCBD07BFEC36EACF7B23C720A4D855'
        'mod/capie.mod'                                             = 'E9B1E3B5133B18D792D676835F4B35388C681D928C5BBF5DA0F22C09C05864ED'
        'mod/capie/descriptor.mod'                                  = '13CD5ABD60E8D55A52FE6B70C3DB5F781DE16FA0C0D89310E6F613686FC5F427'
        'mod/capie/gfx/interface/capie_menu_mark.dds'               = '6842E6D83D773648EE7A5878EA5B0A2E77C66353C926406CFE95B23C88B2B522'
        'mod/capie/interface/capie_menu.gfx'                        = 'D3B6C8221365C6499CEEE2611FEF7DAEE74884B578AD2C90C4F20490EF11E952'
        'mod/capie/interface/frontendmainview.gui'                  = '901F62142B80FEEF88892A64B0DE4D7B96B2B448967D12156BAF2415C6A15918'
        'mod/capie/localisation/english/capie_switch_l_english.yml' = '3959220345820E9F5B03C2882CFC741584451E18757CF7ADBAEBA914CC249437'
        'mod/capie/localisation/polish/capie_switch_l_polish.yml'   = '613C322D04AF3719E9DFFB912B16356D9ABBCB1FAC770C92380D585400C084FF'
        'mod/capie/thumbnail.png'                                   = 'D4BB22A36F572D354E49DCFB54DCF03F03BF844517F261FF2B9BC2D94CFD66FE'
    }
    # hoi4.exe builds this release's DLLs support (install notice, -Update, -CheckUpdate; the launcher enforces it).
    SupportedGameSha256 = @('CF781ED178CD801643B233B7E3FCD1C0EFD19BD01AECD24BF07A35F69F10476F')
}

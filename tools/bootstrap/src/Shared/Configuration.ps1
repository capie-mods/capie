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
    Version             = '1.5.1'
    ZipName             = 'CAPIE-1.5.1-win64.zip'
    ZipSha256           = 'D3888B5AF618EE960AE7E1D25A6D11E3C8A2A347368BE1E0AA29F0E8F875A8C3'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe'                                = 'DFDEF459FB091BFB2F5111AB407F222ED2C015E727AAEE4E6A366F95D2043A08'
        'LICENSE.MinHook.txt'                                       = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/capie_core.dll'                                    = '0CFC527FB9BAC5F64E75921BCFAC24ABB37D7AF60F6C13B7A9626A37B0BB5C61'
        'runtime/capie_editor.dll'                                  = 'F24631D03F073867A95B0E1D2F98E78BD7885EB6C7325A65560BCB7A53EE98D7'
        'runtime/capie_injector.exe'                                = '40BBB4E34056771B502496864145848157F5FAD107F145BF4E237CD013336517'
        'proxy/version.dll'                                         = '9F8ECEA50E7E58E1A72AAA14E7F82BC135AD905FEEBBFD5A9669816BFEFA6F94'
        'mod/capie.mod'                                             = 'A1512E4AFE21B7092B27D3BA5A6C436404A9895AADC1E41B55FACD42A96E98A0'
        'mod/capie/descriptor.mod'                                  = '2DE22DF5EC6BAE2CD3DC1A5669BA8E0A0F817625550E63F7D8477075A235EAC4'
        'mod/capie/gfx/interface/capie_menu_mark.dds'               = '6842E6D83D773648EE7A5878EA5B0A2E77C66353C926406CFE95B23C88B2B522'
        'mod/capie/interface/capie_menu.gfx'                        = 'D3B6C8221365C6499CEEE2611FEF7DAEE74884B578AD2C90C4F20490EF11E952'
        'mod/capie/interface/frontendmainview.gui'                  = '901F62142B80FEEF88892A64B0DE4D7B96B2B448967D12156BAF2415C6A15918'
        'mod/capie/localisation/english/capie_switch_l_english.yml' = '3959220345820E9F5B03C2882CFC741584451E18757CF7ADBAEBA914CC249437'
        'mod/capie/localisation/polish/capie_switch_l_polish.yml'   = '613C322D04AF3719E9DFFB912B16356D9ABBCB1FAC770C92380D585400C084FF'
        'mod/capie/thumbnail.png'                                   = 'D4BB22A36F572D354E49DCFB54DCF03F03BF844517F261FF2B9BC2D94CFD66FE'
    }
    # hoi4.exe builds this release's DLLs support (install notice, -Update, -CheckUpdate; the launcher enforces it).
    SupportedGameSha256 = @('B6650C85002374218E60598BD04AAA26CBAD996FEDCE77126FCF92FC6BD6D888')
}

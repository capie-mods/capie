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
    Version             = '1.6.2'
    ZipName             = 'CAPIE-1.6.2-win64.zip'
    ZipSha256           = '6085C17447A4803D7A97249A6367D9F4CF9306693222A3BEEAC358C82333E6A6'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe' = 'EC8440B8ACCABF89D24F71CAF78EC3307308ADA7A3A0171B698C0F5855183656'
        'LICENSE.MinHook.txt'        = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/capie_core.dll'     = '11E162547A7CA49F1AB9074DB1B729239126BDF940F0F0092A17D4A92DDBCCCC'
        'runtime/capie_editor.dll'   = 'D58B7EBC23E10BE1E91455A59818F2C5B98BF9E5C042202BA3C1D4AABAAE52AC'
        'runtime/capie_injector.exe' = '94902DCFDCAF8CE8DFF849F3346320B10CF9068541ABCD18669E16865B08BE47'
        'mod/capie.mod'              = 'FABB7645BBC6FE9D95CF4B95D591889BC5E8C068356EBFA67ACD4D0A03F629BC'
        'mod/capie/descriptor.mod'   = 'D828206E6E0D083C5E137112EF0242C212EEF388117FA0E8B83E4B82B399DCF2'
        'mod/capie/thumbnail.png'    = 'D4BB22A36F572D354E49DCFB54DCF03F03BF844517F261FF2B9BC2D94CFD66FE'
    }
    # hoi4.exe builds this release's DLLs support (install notice, -Update, -CheckUpdate; the launcher enforces it).
    SupportedGameSha256 = @('709F916AFBEBCFE64A26678702691AEB40945D206074C2A80DE8E9D601FCE148')
    # Separate proxy asset: downloaded only for -LoadMethod Proxy (the default Launcher install never reads it).
    Proxy               = [ordered]@{
        ZipName   = 'CAPIE-1.6.2-proxy-win64.zip'
        ZipSha256 = '0DAAC7C72468F468747B566BACC3678FAF01065C5C0C9F6A2DE9B224911022A7'
        Files     = [ordered]@{ 'proxy/version.dll' = 'F21EC5799388B1182BF0F4D2F4BB3704E0E7D702C86880BEDE5BCF19758942CC' }
    }
}

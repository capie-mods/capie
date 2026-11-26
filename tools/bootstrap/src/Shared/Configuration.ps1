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
    Version             = '1.7.3'
    ZipName             = 'CAPIE-1.7.3-win64.zip'
    ZipSha256           = '2AAAA4EA595A56470C73220344B7D27F3562A4FC778A1C440344198C135F866B'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe' = '445A3A02624041F55A85AD4C96B992B3092ED0F6F4EEAC477354D8C85317AE47'
        'LICENSE.MinHook.txt'        = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/capie_core.dll'     = '4E0F3F535A12847392DAC20DDDF3ED63258E00E6AEF4B00D22D0885E274EE965'
        'runtime/capie_editor.dll'   = 'B392B0150612B56F9D1BC1548632424353AB175AB0E50E195BA7DC8DFAC7D546'
        'runtime/capie_injector.exe' = '182DE13567D0925D7E7A13161C9F7549EEA831F162C426D235AFCD6D4ADF5358'
        'mod/capie.mod'              = '4B4E52AFA2F4A5865F962E3A7CACD996B9766235689798B17600A1AE815DF967'
        'mod/capie/descriptor.mod'   = '4F3669535A65DF48E581504649BEDA21EC5587DBDFE9435B05DC60D0B5F7218A'
        'mod/capie/thumbnail.png'    = 'D4BB22A36F572D354E49DCFB54DCF03F03BF844517F261FF2B9BC2D94CFD66FE'
    }
    # hoi4.exe builds this release's DLLs support (install notice, -Update, -CheckUpdate; the launcher enforces it).
    SupportedGameSha256 = @('B6EAC19F0EBF966A2BF2025C0FD1BAA187468E4078AD15BCFBB712A26F65E432')
    # Separate proxy asset: downloaded only for -LoadMethod Proxy (the default Launcher install never reads it).
    Proxy               = [ordered]@{
        ZipName   = 'CAPIE-1.7.3-proxy-win64.zip'
        ZipSha256 = 'EB60AA90B69C94ADD3BF148E7892BCAD6021BC67BFEDD58601847BD1BBC50767'
        Files     = [ordered]@{ 'proxy/version.dll' = '6B3AA24380C480FF3D7790C50E38DBA05D51C78ABC716CB67E170407A0DCC14D' }
    }
}

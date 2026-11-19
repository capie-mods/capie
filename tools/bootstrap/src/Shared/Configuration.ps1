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
    Version             = '1.7.2'
    ZipName             = 'CAPIE-1.7.2-win64.zip'
    ZipSha256           = '86D881DAC1F15500664011A46E18FAAF76904E65F38ABB755B4BC8162D577130'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe' = 'CF25FCD67209D8D844176E414EB78933363BF56CDB46405CFEE44BE163F2F0DA'
        'LICENSE.MinHook.txt'        = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/capie_core.dll'     = '118A2D83A1AC944D18DE9C73B55F8DA2D93CED776DC01DAABE80945727099985'
        'runtime/capie_editor.dll'   = '34421B65C2320FA461C8D7DC04C4DF12A49E45C3261A40A35D9E0E4A9DD35AA2'
        'runtime/capie_injector.exe' = '427995D5A2E31C9757F5A83A0FFE3DDB5295BE2C59FD9993109FFEECAF10FF51'
        'mod/capie.mod'              = '4B4E52AFA2F4A5865F962E3A7CACD996B9766235689798B17600A1AE815DF967'
        'mod/capie/descriptor.mod'   = '4F3669535A65DF48E581504649BEDA21EC5587DBDFE9435B05DC60D0B5F7218A'
        'mod/capie/thumbnail.png'    = 'D4BB22A36F572D354E49DCFB54DCF03F03BF844517F261FF2B9BC2D94CFD66FE'
    }
    # hoi4.exe builds this release's DLLs support (install notice, -Update, -CheckUpdate; the launcher enforces it).
    SupportedGameSha256 = @('1BEB2A38FE2D747E5B7ADB9B9648E350B0B851845DD228FFB87D2D2AC1B63A9F')
    # Separate proxy asset: downloaded only for -LoadMethod Proxy (the default Launcher install never reads it).
    Proxy               = [ordered]@{
        ZipName   = 'CAPIE-1.7.2-proxy-win64.zip'
        ZipSha256 = 'E1668F14762EE47AF46DE700D616FB69192884A0624BDCA3A37D1E505F72C80B'
        Files     = [ordered]@{ 'proxy/version.dll' = 'BEDB1B7A3AF2C5F9FBCFBF1A08FCD606566E9DF3994D1E236938951F2051D991' }
    }
}

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
    Version             = '0.1.0'
    ZipName             = 'RenameAnything-0.1.0-win64.zip'
    ZipSha256           = 'TO-BE-PINNED-AT-RELEASE'
    Files               = [ordered]@{
        'RenameAnythingLauncher.exe'                       = '1AEAC3C9B0717491E239A5C263AB27ECF53DDD536317DEA69CCCADC26B9C785A'
        'LICENSE.MinHook.txt'                              = '4F21F857550D7BE854DA6EA5F2DA4E6775CA4E3FBB535E4F3D961C47D0BF3335'
        'runtime/rename_anything_country.dll'              = '18841A6D7552224F0D34BBC05983916912969735109A548C16DE4581845E10B5'
        'runtime/rename_anything_injector.exe'             = 'DF8D60E9F1C403193DAC79FE5229793569E30D01D17F5172C5B11F1CB14063C7'
        'runtime/rename_anything_inline_editor_bridge.dll' = 'B20FBE88756F7F22D989230D6D51953D82BCCD2349CD00BDCC545C382DF8BAF6'
        'runtime/inject_inline_editor_render_bridge.exe'   = '31B510BECD5DCBA3588231C8312C32B1F2C2CC49B0CAFC8BBB2A51B18F455AE4'
    }
    # hoi4.exe builds this release's DLLs support (for -CheckUpdate messages; the launcher enforces it).
    SupportedGameSha256 = @('7DC947BE34970DA1E1C787BCDDBE7F62B610FF258AEBF8F06AA8B348F3A031D5')
}

param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir,

    [string]$OutputDir = "",
    [string]$Version = "0.1.0",
    [string]$Publisher = "CN=insys",
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Architecture = "x64",
    [string]$MakeAppxPath = "",
    [string]$SignToolPath = "",
    [string]$CertificatePath = "",
    [string]$CertificatePassword = "",
    [string]$TimestampUrl = "",
    [switch]$SelfSign
)

$ErrorActionPreference = "Stop"

if (-not $CertificatePassword) {
    $CertificatePassword = $env:INSYS_MSIX_CERTIFICATE_PASSWORD
}

function Fail([string]$Message) {
    Write-Error $Message
    exit 1
}

function Resolve-SdkTool([string]$ToolName, [string]$Override) {
    if ($Override) {
        if (-not (Test-Path -LiteralPath $Override)) {
            Fail "SDK tool not found: $Override"
        }
        return (Resolve-Path -LiteralPath $Override).Path
    }
    $roots = @(
        (Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"),
        (Join-Path $env:ProgramFiles "Windows Kits\10\bin")
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
    foreach ($root in $roots) {
        $candidate = Get-ChildItem -LiteralPath $root -Directory -Filter "10.*" |
            Sort-Object Name -Descending |
            ForEach-Object {
                $exe = Join-Path $_.FullName "x64\$ToolName"
                if (Test-Path -LiteralPath $exe) { $exe }
            } |
            Select-Object -First 1
        if ($candidate) {
            return $candidate
        }
    }
    Fail "$ToolName was not found in the Windows 10/11 SDK. Install the Windows SDK or pass the tool path explicitly."
}

function Normalize-MsixVersion([string]$Text) {
    $parts = @($Text.Split(".") | ForEach-Object { $_.Trim() })
    if ($parts.Count -lt 1 -or $parts.Count -gt 4) {
        Fail "Version must have one to four numeric components: $Text"
    }
    while ($parts.Count -lt 4) { $parts += "0" }
    foreach ($part in $parts) {
        $number = 0
        if (-not [int]::TryParse($part, [ref]$number) -or $number -lt 0 -or $number -gt 65535) {
            Fail "Invalid MSIX version component: $part"
        }
    }
    return ($parts -join ".")
}

if (-not (Test-Path -LiteralPath $InstallDir)) {
    Fail "Install directory not found: $InstallDir"
}
$InstallDir = (Resolve-Path -LiteralPath $InstallDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $InstallDir "insys_editor.exe"))) {
    Fail "insys_editor.exe not found in $InstallDir"
}
if (-not (Test-Path -LiteralPath (Join-Path $InstallDir "web\index.html"))) {
    Fail "web/index.html not found in $InstallDir"
}

if (-not $OutputDir) {
    $OutputDir = Join-Path (Split-Path -Parent $InstallDir) "package"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$msixVersion = Normalize-MsixVersion $Version
$stage = Join-Path $env:TEMP ("insys-msix-" + [guid]::NewGuid().ToString("N"))
$assetsSource = Join-Path $PSScriptRoot "Assets"
$manifestTemplate = Join-Path $PSScriptRoot "AppxManifest.xml.in"
try {
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item -Path (Join-Path $InstallDir "*") -Destination $stage -Recurse -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $stage "Assets") | Out-Null
    Copy-Item -Path (Join-Path $assetsSource "*") -Destination (Join-Path $stage "Assets") -Force

    $publisherXml = [System.Security.SecurityElement]::Escape($Publisher)
    $manifest = Get-Content -LiteralPath $manifestTemplate -Raw -Encoding UTF8
    $manifest = $manifest.Replace("__PUBLISHER__", $publisherXml)
    $manifest = $manifest.Replace("__VERSION__", $msixVersion)
    $manifest = $manifest.Replace("__ARCH__", $Architecture)
    $manifestPath = Join-Path $stage "AppxManifest.xml"
    [System.IO.File]::WriteAllText($manifestPath, $manifest, (New-Object System.Text.UTF8Encoding($false)))

    $packageName = "insys-editor-$Version-windows-$Architecture"
    $msixPath = Join-Path $OutputDir "$packageName.msix"
    if (Test-Path -LiteralPath $msixPath) {
        Remove-Item -LiteralPath $msixPath -Force
    }

    $makeappx = Resolve-SdkTool "makeappx.exe" $MakeAppxPath
    & $makeappx pack /d $stage /p $msixPath /o
    if ($LASTEXITCODE -ne 0) {
        Fail "makeappx failed with exit code $LASTEXITCODE"
    }

    $cerPath = ""
    if ($SelfSign -or $CertificatePath) {
        $signtool = Resolve-SdkTool "signtool.exe" $SignToolPath
        if ($SelfSign -and -not $CertificatePath) {
            $certificate = New-SelfSignedCertificate `
                -Type CodeSigningCert `
                -Subject $Publisher `
                -CertStoreLocation "Cert:\CurrentUser\My" `
                -KeyUsage DigitalSignature `
                -FriendlyName "insys editor MSIX development" `
                -NotAfter (Get-Date).AddYears(2)
            $password = [guid]::NewGuid().ToString("N")
            $securePassword = ConvertTo-SecureString -String $password -AsPlainText -Force
            $CertificatePath = Join-Path $OutputDir "$packageName-dev.pfx"
            $cerPath = Join-Path $OutputDir "$packageName-dev.cer"
            Export-PfxCertificate -Cert $certificate -FilePath $CertificatePath -Password $securePassword | Out-Null
            Export-Certificate -Cert $certificate -FilePath $cerPath | Out-Null
            $CertificatePassword = $password
            Write-Host "Created development certificate: $certificate"
        }
        if (-not (Test-Path -LiteralPath $CertificatePath)) {
            Fail "Certificate not found: $CertificatePath"
        }
        $signArgs = @("sign", "/fd", "SHA256", "/f", $CertificatePath)
        if ($CertificatePassword) {
            $signArgs += @("/p", $CertificatePassword)
        }
        if ($TimestampUrl) {
            $signArgs += @("/tr", $TimestampUrl, "/td", "SHA256")
        }
        $signArgs += $msixPath
        & $signtool @signArgs
        if ($LASTEXITCODE -ne 0) {
            Fail "signtool failed with exit code $LASTEXITCODE"
        }
        if ($cerPath) {
            Write-Host "Development certificate export: $cerPath"
            Write-Host "Install it into Trusted People before installing the MSIX."
        }
    }

    $hash = (Get-FileHash -LiteralPath $msixPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumPath = "$msixPath.sha256"
    [System.IO.File]::WriteAllText(
        $checksumPath,
        "$hash  $(Split-Path -Leaf $msixPath)`n",
        (New-Object System.Text.UTF8Encoding($false)))

    Write-Host "MSIX: $msixPath"
    Write-Host "SHA256: $checksumPath"
    Write-Host "Version: $msixVersion"
    Write-Host "Publisher: $Publisher"
    Write-Host "Architecture: $Architecture"
} finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
}

param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$WledPath,

  [Parameter(Position = 1)]
  [string]$PioEnv = 'esp32dev',

  [string]$DeployHost,

  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$usermodDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$usermodDir = (Resolve-Path $usermodDir).Path
$wledDir = (Resolve-Path $WledPath).Path
$usermodName = 'wled-sprite-usermod'
$altConf = 'platformio.sprite.ini'
$altEnv = "${PioEnv}_sprite_usermod"
$platformioIni = Join-Path $wledDir 'platformio.ini'
$altConfPath = Join-Path $wledDir $altConf
$usermodSpec = "$usermodName = symlink://$($usermodDir -replace '\\', '/')"
$usermodLinkPath = Join-Path $wledDir ".pio\libdeps\$altEnv\$usermodName.pio-link"
$firmwarePath = Join-Path $wledDir ".pio\build\$altEnv\firmware.bin"

if (-not (Test-Path $platformioIni -PathType Leaf)) {
  throw "WLED checkout not found at: $wledDir"
}

function Assert-StockWledCheckout {
  param([string]$RepoPath)

  $gitDir = Join-Path $RepoPath '.git'
  if (-not (Test-Path -LiteralPath $gitDir)) {
    throw "Expected a git checkout at: $RepoPath"
  }

  $originUrl = (git -C $RepoPath remote get-url origin 2>$null)
  if (-not $originUrl) {
    throw "Could not read git origin remote from: $RepoPath"
  }

  # Accept any WLED fork (github.com/<owner>/WLED). The check exists to catch a
  # totally unrelated repo, not to lock the build to upstream — forks are fine.
  $normalized = $originUrl.ToLowerInvariant()
  if ($normalized -notmatch '(github\.com[:/])[^/]+/wled(\.git)?$') {
    throw "Expected a WLED fork (github.com/<owner>/WLED), got: $originUrl"
  }
}

Assert-StockWledCheckout -RepoPath $wledDir

if ($SkipBuild -and -not $DeployHost) {
  throw "-SkipBuild requires -DeployHost."
}

function New-PlatformIoSpriteConfig {
  param(
    [string]$SourceIni,
    [string]$DestinationIni,
    [string]$BaseEnv,
    [string]$DerivedEnv,
    [string]$UsermodSpec
  )

  $output = New-Object System.Collections.Generic.List[string]
  $skipExtraConfigEntries = $false

  foreach ($line in Get-Content -LiteralPath $SourceIni) {
    if ($line -match '^extra_configs\s*=') {
      $output.Add($line)
      $skipExtraConfigEntries = $true
      continue
    }

    if ($skipExtraConfigEntries -and $line -match 'platformio_override\.ini') {
      continue
    }

    if ($skipExtraConfigEntries -and $line -match 'platformio_release\.ini') {
      $output.Add($line)
      $skipExtraConfigEntries = $false
      continue
    }

    $output.Add($line)
  }

  $output.Add('')
  $output.Add("[env:$DerivedEnv]")
  $output.Add("extends = env:$BaseEnv")
  $output.Add('custom_usermods =')
  $output.Add("  `${env:$BaseEnv.custom_usermods}")
  $output.Add("  $UsermodSpec")

  Set-Content -LiteralPath $DestinationIni -Value $output -Encoding ascii
}

function Get-WledInfo {
  param([string]$DeviceHost)

  Invoke-RestMethod -Uri "http://$DeviceHost/json/info" -TimeoutSec 10
}

function Deploy-Firmware {
  param(
    [string]$DeviceHost,
    [string]$Firmware
  )

  if (-not (Test-Path -LiteralPath $Firmware -PathType Leaf)) {
    throw "Built firmware not found at: $Firmware"
  }

  $preDeployInfo = $null
  try {
    $preDeployInfo = Get-WledInfo -DeviceHost $DeviceHost
  }
  catch {
  }

  Write-Host "Uploading firmware to $DeviceHost..."

  try {
    $invokeWebRequest = Get-Command Invoke-WebRequest
    if ($invokeWebRequest.Parameters.ContainsKey('Form')) {
      $response = Invoke-WebRequest -Uri "http://$DeviceHost/update" -Method Post -Form @{
        update = Get-Item -LiteralPath $Firmware
      } -TimeoutSec 600
    }
    else {
      $curl = Get-Command curl.exe -ErrorAction Stop
      $curlOutput = & $curl.Source -sS -f -F "update=@$Firmware" "http://$DeviceHost/update" 2>&1
      if ($LASTEXITCODE -ne 0) {
        throw ($curlOutput -join "`n")
      }
      $response = [pscustomobject]@{
        StatusCode = 200
        Content = ($curlOutput -join "`n")
      }
    }
  }
  catch {
    $detail = $_.ErrorDetails.Message
    if (-not $detail) {
      $detail = $_.Exception.Message
    }
    throw "OTA upload to $DeviceHost failed: $detail"
  }

  if ($response.StatusCode -ne 200 -or $response.Content -notmatch 'Update successful!') {
    $message = ($response.Content -replace '\s+', ' ').Trim()
    if (-not $message) {
      $message = "Unexpected HTTP status $($response.StatusCode)."
    }
    throw "OTA update failed: $message"
  }

  $deadline = [DateTime]::UtcNow.AddMinutes(2)
  $postDeployInfo = $null
  do {
    try {
      $postDeployInfo = Get-WledInfo -DeviceHost $DeviceHost
      if (-not $preDeployInfo -or $postDeployInfo.uptime -lt $preDeployInfo.uptime) {
        if ($null -eq $postDeployInfo.sprite_usermod -or -not $postDeployInfo.sprite_usermod.ready) {
          throw "Panel rebooted but sprite_usermod is not ready after deployment."
        }
        return $postDeployInfo
      }
    }
    catch {
      $lastError = $_.Exception.Message
    }

    Start-Sleep -Seconds 1
  } while ([DateTime]::UtcNow -lt $deadline)

  if ($postDeployInfo) {
    throw "Uploaded firmware to $DeviceHost but reboot could not be confirmed within 2 minutes."
  }

  if ($lastError) {
    throw "Uploaded firmware to $DeviceHost but verification failed: $lastError"
  }

  throw "Uploaded firmware to $DeviceHost but it did not come back online within 2 minutes."
}

Push-Location $wledDir
try {
  if (-not $SkipBuild) {
    New-PlatformIoSpriteConfig -SourceIni $platformioIni -DestinationIni $altConfPath -BaseEnv $PioEnv -DerivedEnv $altEnv -UsermodSpec $usermodSpec

    if (Test-Path -LiteralPath $usermodLinkPath) {
      Remove-Item -LiteralPath $usermodLinkPath -Force
    }

    platformio run -c $altConf -e $altEnv
  }

  if ($DeployHost) {
    $deployInfo = Deploy-Firmware -DeviceHost $DeployHost -Firmware $firmwarePath
    Write-Host "Deployment verified on $DeployHost (uptime: $($deployInfo.uptime)s, ip: $($deployInfo.ip))."
  }
}
finally {
  Pop-Location
}

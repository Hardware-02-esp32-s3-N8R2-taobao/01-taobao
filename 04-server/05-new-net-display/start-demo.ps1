$nodePath = "C:\Program Files\nodejs\node.exe"

if (-not (Test-Path $nodePath)) {
  Write-Error "Node.js not found at $nodePath"
  exit 1
}

Set-Location $PSScriptRoot
& $nodePath "server.js"

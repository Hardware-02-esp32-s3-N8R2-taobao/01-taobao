$targets = @(
  "http://192.168.2.123:3000/api/public-url",
  "http://vm-Server:3000/api/public-url",
  "http://vm-server.local:3000/api/public-url"
)

foreach ($target in $targets) {
  try {
    $response = Invoke-RestMethod -Uri $target -TimeoutSec 5
    if ($response.url) {
      $response.url
      exit 0
    }
  } catch {
  }
}

Write-Error "Unable to fetch public URL from any configured NetDisplay HTTP target."
exit 1

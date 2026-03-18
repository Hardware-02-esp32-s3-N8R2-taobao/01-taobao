const fs = require("fs");
const path = require("path");
const { spawn } = require("child_process");

const candidatePaths = [
  "C:\\Users\\10243\\AppData\\Local\\Microsoft\\WinGet\\Packages\\Cloudflare.cloudflared_Microsoft.Winget.Source_8wekyb3d8bbwe\\cloudflared.exe",
  "C:\\Program Files\\cloudflared\\cloudflared.exe",
  "/home/zerozero/.local/bin/cloudflared",
  "/usr/local/bin/cloudflared",
  "/usr/bin/cloudflared"
];

const cloudflaredPath = candidatePaths.find((item) => fs.existsSync(item)) || "cloudflared";

if (!cloudflaredPath) {
  console.error("cloudflared.exe not found");
  process.exit(1);
}

const logPath = path.join(__dirname, "cloudflared.log");
const errPath = path.join(__dirname, "cloudflared.err.log");
const pidPath = path.join(__dirname, "cloudflared.pid");

for (const filePath of [logPath, errPath, pidPath]) {
  if (fs.existsSync(filePath)) {
    fs.unlinkSync(filePath);
  }
}

const out = fs.openSync(logPath, "a");
const err = fs.openSync(errPath, "a");

const child = spawn(
  cloudflaredPath,
  ["tunnel", "--url", "http://localhost:3000", "--protocol", "http2", "--no-autoupdate"],
  {
    detached: true,
    stdio: ["ignore", out, err]
  }
);

fs.writeFileSync(pidPath, String(child.pid), "utf8");
child.unref();

console.log(`cloudflared started, pid=${child.pid}`);

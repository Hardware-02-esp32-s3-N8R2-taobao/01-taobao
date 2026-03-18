const fs = require("fs");
const path = require("path");

const pidPath = path.join(__dirname, "cloudflared.pid");

if (!fs.existsSync(pidPath)) {
  console.error("cloudflared.pid not found");
  process.exit(1);
}

const pid = Number(fs.readFileSync(pidPath, "utf8").trim());

if (!Number.isFinite(pid)) {
  console.error("invalid pid");
  process.exit(1);
}

try {
  process.kill(pid);
  fs.unlinkSync(pidPath);
  console.log(`stopped pid=${pid}`);
} catch (error) {
  console.error(`failed to stop pid=${pid}: ${error.message}`);
  process.exit(1);
}

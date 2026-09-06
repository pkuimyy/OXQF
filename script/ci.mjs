import { spawnSync } from "node:child_process";

const steps = [
  ["npm", ["test"]],
  ["cmake", ["--preset", "dev"]],
  ["cmake", ["--build", "--preset", "dev", "--config", "Debug"]],
  ["ctest", ["--preset", "dev", "-C", "Debug"]],
  ["cmake", ["--preset", "clang-sanitize"]],
  ["cmake", ["--build", "--preset", "clang-sanitize", "--config", "Debug"]],
  ["ctest", ["--preset", "clang-sanitize", "-C", "Debug"]],
];

for (const [command, arguments_] of steps) {
  console.log(`\n> ${command} ${arguments_.join(" ")}`);
  const result = spawnSync(command, arguments_, {
    cwd: process.cwd(),
    env: process.env,
    stdio: "inherit",
  });

  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
